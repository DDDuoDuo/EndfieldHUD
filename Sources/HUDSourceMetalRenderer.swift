import AppKit
import MetalKit
import CryptoKit
import simd

/// Source geometry and translated source programs. Camera policy and animation
/// values are supplied by the scene; this view does not fit source art to the
/// earlier desktop composition or synthesize a postprocess.
final class HUDSourceMetalRenderer: MTKView, MTKViewDelegate {
    enum SceneColorMode: Hashable {
        case directLDR
        /// Original packed RGB target followed by UberPost_CompositeUI. This
        /// has no alpha channel and requires a real backdrop for desktop use.
        case sourceRGBHDR
    }

    struct SceneColorReadback {
        var data: Data
        var width: Int
        var height: Int
        var rowBytes: Int
        var pixelFormat: String
    }

    struct Camera {
        var viewProjection: simd_float4x4
        var viewNoTranslationProjection: simd_float4x4
        var worldSpacePosition: SIMD3<Float>
        var timeSeconds: Float
        var renderPathInjected: Float
        var flipX: Float
        var flipY: Float
        // Required by the source TMP gradient calculation. This is the
        // projection alone, under the same explicit GPU Y policy as VP.
        var projection: simd_float4x4? = nil
        var inverseView: simd_float4x4? = nil
        var uiProjectionParameters: SIMD4<Float>? = nil
    }

    struct StencilState: Hashable {
        var reference: UInt32 = 0
        var compare: Int = 8
        var pass: Int = 0
        var fail: Int = 0
        var depthFail: Int = 0
        var readMask: UInt32 = 255
        var writeMask: UInt32 = 255
    }

    struct Batch {
        var mesh: String
        var material: String
        var world: simd_float4x4
        var color: SIMD4<Float>
        /// Profile artwork uses its independently selected palette. Source
        /// and ordinary desktop batches keep the existing global tint policy.
        var appliesDesktopAccent: Bool = true
        var uniformOverrides: [String: [Float]] = [:]
        var textureOverrides: [String: String] = [:]
        var stencilOverrides: StencilState? = nil
        // Unity source bits: R=1, G=2, B=4, A=8.
        var colorWriteMask: UInt8? = nil
        var indexRange: Range<Int>? = nil
        /// Original scene instance for diagnostic readback and renderer-wide
        /// property-block checks; does not participate in GPU shading.
        var sourceNodeID: String? = nil
    }

    #if HUD_SOURCE_RENDER_PREVIEW
    /// Read-only fixture access to the buffers chosen by the current draw.
    /// The caller waits with copyDrawableImage() before exporting. This is
    /// absent from the shipped app build.
    struct PreviewGeometry {
        var positions: [SIMD4<Float>]
        var uv: [SIMD2<Float>]
        var originalColors: [SIMD4<Float>]
        var uploadedColors: [SIMD4<Float>]
        var indices: [UInt32]
        var vertexBytes: Data
        var indexBytes: Data
        var vertexStride: Int
        var vertexOffsets: [String: Int]
    }

    struct PreviewPass {
        var id: String
        var shader: String
        var vertexFile: String
        var fragmentFile: String
        var materialValues: [String: [Float]]
        var textures: [String: String]
        var alphaBlend: [String: Int]
        var vertexAttributes: [[String: Int]]
    }

    func previewGeometry(for batch: Batch) throws -> PreviewGeometry {
        guard renderedFrameGeneration == submittedFrameGeneration,
              let geometry = geometries[batch.mesh] else {
            throw Failure.message("Preview geometry has no completed current draw: " + batch.mesh)
        }
        let buffer: MTLBuffer
        if batch.color == SIMD4<Float>(repeating: 1) { buffer = geometry.vertices }
        else if let cached = tintedVertices[batch.mesh], cached.color == batch.color,
                cached.geometryRevision == geometry.revision { buffer = cached.buffer }
        else { throw Failure.message("Current preview tint buffer unavailable: " + batch.mesh) }
        guard buffer.storageMode == .shared, geometry.indices.storageMode == .shared,
              buffer.length >= geometry.originalVertices.count * MemoryLayout<Vertex>.stride,
              geometry.indices.length >= geometry.indexCount * MemoryLayout<UInt32>.stride else {
            throw Failure.message("Preview requires original CPU-visible shared buffers: " + batch.mesh)
        }
        let vertices = Array(UnsafeBufferPointer(start: buffer.contents().assumingMemoryBound(to: Vertex.self),
            count: geometry.originalVertices.count))
        guard vertices.map(\.position) == geometry.originalVertices.map(\.position),
              vertices.map(\.uv) == geometry.originalVertices.map(\.uv) else {
            throw Failure.message("Preview uploaded positions/UV differ from registered geometry: " + batch.mesh)
        }
        let indices = Array(UnsafeBufferPointer(start: geometry.indices.contents().assumingMemoryBound(to: UInt32.self),
            count: geometry.indexCount))
        return PreviewGeometry(positions: vertices.map(\.position), uv: vertices.map(\.uv),
            originalColors: geometry.originalVertices.map(\.color), uploadedColors: vertices.map(\.color), indices: indices,
            vertexBytes: Data(bytes: buffer.contents(), count: vertices.count * MemoryLayout<Vertex>.stride),
            indexBytes: Data(bytes: geometry.indices.contents(), count: indices.count * MemoryLayout<UInt32>.stride),
            vertexStride: MemoryLayout<Vertex>.stride,
            vertexOffsets: ["position": MemoryLayout<Vertex>.offset(of: \Vertex.position) ?? -1,
                            "uv": MemoryLayout<Vertex>.offset(of: \Vertex.uv) ?? -1,
                            "color": MemoryLayout<Vertex>.offset(of: \Vertex.color) ?? -1])
    }

    func previewPasses(for batch: Batch) throws -> [PreviewPass] {
        guard let material = materials[batch.material] else {
            throw Failure.message("Preview material unavailable: " + batch.material)
        }
        let values = material.values.merging(batch.uniformOverrides) { _, override in override }
        return material.passes.map { pass in
            let attachment = pass.pipelineDescriptor.colorAttachments[0]!
            let vertex = pass.pipelineDescriptor.vertexDescriptor
            var attributes: [[String: Int]] = []
            for index in 0..<31 {
                guard let attribute = vertex?.attributes[index], attribute.format != .invalid else { continue }
                attributes.append(["attribute": index, "offset": attribute.offset,
                    "format": Int(attribute.format.rawValue), "bufferIndex": attribute.bufferIndex,
                    "stride": vertex?.layouts[attribute.bufferIndex].stride ?? -1])
            }
            var textures: [String: String] = [:]
            for binding in pass.shader.textures {
                textures[binding.name] = batch.textureOverrides[binding.name] ?? material.textures[binding.name] ?? "__white"
            }
            return PreviewPass(id: pass.id, shader: pass.shader.shader,
                vertexFile: pass.shader.stages["vertex"]?.file ?? "",
                fragmentFile: pass.shader.stages["fragment"]?.file ?? "",
                materialValues: values,
                textures: textures,
                alphaBlend: ["enabled": attachment.isBlendingEnabled ? 1 : 0,
                    "source": Int(attachment.sourceAlphaBlendFactor.rawValue),
                    "destination": Int(attachment.destinationAlphaBlendFactor.rawValue),
                    "operation": Int(attachment.alphaBlendOperation.rawValue),
                    "writeMask": Int(batch.colorWriteMask ?? UInt8(attachment.writeMask.rawValue))],
                vertexAttributes: attributes)
        }
    }
    #endif

    private struct Vertex {
        var position: SIMD4<Float>
        var uv: SIMD2<Float>
        var padding = SIMD2<Float>(repeating: 0)
        var color: SIMD4<Float>
        var normal = SIMD4<Float>(repeating: 0)
        var uv1 = SIMD2<Float>(repeating: 0)
        var tailPadding = SIMD2<Float>(repeating: 0)
    }

    private struct Geometry {
        var vertices: MTLBuffer
        var indices: MTLBuffer
        var indexCount: Int
        var originalVertices: [Vertex]
        var hasTextChannels = false
        var revision: UInt64 = 0
    }
    private struct MergedMember: Equatable {
        let mesh: String
        let revision: UInt64
        let color: SIMD4<Float>
    }
    private struct MergedGeometry {
        let members: [MergedMember]
        let geometry: Geometry
    }

    private struct Field: Decodable {
        var name: String
        var offset: Int
    }

    private struct Uniform: Decodable {
        var name: String
        var size: Int
        var index: Int
        var fields: [Field]
    }

    private struct Stage: Decodable {
        var file: String
        var function: String
        var uniforms: [Uniform]
    }

    private struct Shader: Decodable {
        struct TextureBinding: Decodable {
            var name: String
            var index: Int
            var sampler_index: Int?
            var stage: String?
        }
        var shader: String
        var stages: [String: Stage]
        var textures: [TextureBinding]
    }

    /// Parsed source values are immutable CPU data; no Metal state enters the
    /// process-wide metadata catalog.
    private struct MaterialInputs {
        let name: String
        let id: String?
        let values: [String: [Float]]
        let propertyTypes: [String: (type: Int, flags: Int)]
        let textureIDs: [String: String]
        let shaderID: String?
        let keywords: Set<String>
        let clipVariants: [String: String]?
    }

    private struct Material {
        var values: [String: [Float]]
        var textures: [String: String]
        var passes: [Pass]
        var needsText: Bool
        var needsMap: Bool
        var needsSoftMask: Bool
        var needsInverseView: Bool
        var needsUIProjection: Bool
        init(values: [String: [Float]], textures: [String: String], passes: [Pass]) {
            self.values = values; self.textures = textures; self.passes = passes
            needsText = passes.contains { $0.shader.shader == "HGRP/UI/TextMeshPro/Distance Field" }
            needsMap = passes.contains { $0.shader.shader == "HGRP/MinimapTerrain" }
            needsSoftMask = passes.contains { $0.shader.textures.contains { $0.name == "_SoftMaskTex" } }
            let fields = Set(passes.flatMap { $0.shader.stages.values.flatMap { $0.uniforms.flatMap { $0.fields.map(\.name) } } })
            needsInverseView = fields.contains("unity_MatrixInvV") || fields.contains("_InvViewMatrix")
            needsUIProjection = fields.contains("_UIProjectionParams")
        }
    }

    private final class Pass {
        let shaderKey: String
        let pipelineKey: SourcePipelineKey
        var shader: Shader
        var pipeline: MTLRenderPipelineState?
        var uniformByteCounts: [String: [Int: Int]] = [:]
        var uniformPlans: [UniformPlan] = []
        var texturePlans: [TexturePlan] = []
        var depth: MTLDepthStencilState
        var stencilReference: UInt32
        var cull: MTLCullMode
        var id: String
        var pipelineDescriptor: MTLRenderPipelineDescriptor
        var depthCompare: MTLCompareFunction
        var depthWrite: Bool

        init(shaderKey: String, pipelineKey: SourcePipelineKey, shader: Shader,
             depth: MTLDepthStencilState, stencilReference: UInt32, cull: MTLCullMode,
             id: String, pipelineDescriptor: MTLRenderPipelineDescriptor,
             depthCompare: MTLCompareFunction, depthWrite: Bool) {
            self.shaderKey = shaderKey; self.pipelineKey = pipelineKey; self.shader = shader
            self.depth = depth; self.stencilReference = stencilReference; self.cull = cull
            self.id = id; self.pipelineDescriptor = pipelineDescriptor
            self.depthCompare = depthCompare; self.depthWrite = depthWrite
        }
    }

    private enum UniformValue: Equatable {
        case none, world, viewProjection, viewNoTranslation, projection, inverseView
        case uiProjection, cameraPosition, uiTime, time, screen, renderPath, flipX, flipY
    }
    private struct UniformFieldPlan {
        var name: String
        var offset: Int
        var value: [Float]?
        var isColor: Bool
        var dynamic: UniformValue
    }
    private struct UniformPlan {
        var key: String
        var vertex: Bool
        var index: Int
        var byteCount: Int
        var fields: [UniformFieldPlan]
        var fieldNames: Set<String>
        var needsWorld: Bool
        var needsCamera: Bool
        var needsTime: Bool
    }
    private struct UniformCacheKey: Hashable { var plan: String; var mesh: String; var appliesDesktopAccent: Bool }
    private struct SharedUniformKey: Hashable { var plan: String; var appliesDesktopAccent: Bool }
    private struct UniformCacheEntry {
        var overrides: [String: [Float]]
        var world: simd_float4x4?
        var cameraVersion: UInt64
        var timeVersion: UInt64
        var data: Data
    }

    /// One current payload, shared only when its inputs are truly common.
    /// Batch slots resolve property names once; steady draws use these cells
    /// directly, without rediscovering override membership or cache keys.
    private final class PreparedUniformCell {
        let plan: UniformPlan
        let appliesDesktopAccent: Bool
        var data: Data?
        var world: simd_float4x4?
        var cameraVersion: UInt64 = 0
        var timeVersion: UInt64 = 0
        init(plan: UniformPlan, appliesDesktopAccent: Bool = true) {
            self.plan = plan; self.appliesDesktopAccent = appliesDesktopAccent
        }
    }
    private final class PreparedBatchUniforms {
        let mesh: String
        let material: String
        let overrides: [String: [Float]]
        let appliesDesktopAccent: Bool
        var passes: [ObjectIdentifier: [PreparedUniformCell]] = [:]
        init(batch: Batch) {
            mesh = batch.mesh; material = batch.material; overrides = batch.uniformOverrides
            appliesDesktopAccent = batch.appliesDesktopAccent
        }
    }

    private struct TextureAsset {
        var texture: MTLTexture
        var sampler: MTLSamplerState
    }
    private struct TexturePlan {
        let name: String
        let vertex: Bool
        let index: Int
        let samplerIndex: Int
        let defaultID: String
    }
    private struct ResolvedTexture {
        let plan: TexturePlan
        let asset: TextureAsset
    }
    /// Metal bindings persist within an encoder, including across pipeline
    /// switches. This cache starts empty for every encoder; it never carries
    /// render state, inline bytes or resource ownership between frames.
    private struct EncoderBindings {
        var changes = 0
        var skips = 0
        private var pipeline: ObjectIdentifier?
        private var depth: ObjectIdentifier?
        private var stencil: UInt32?
        private var cull: MTLCullMode?
        private var vertexBuffer: ObjectIdentifier?
        private var vertexBytes: [Int: Data] = [:]
        private var fragmentBytes: [Int: Data] = [:]
        private var vertexTextures: [Int: ObjectIdentifier] = [:]
        private var fragmentTextures: [Int: ObjectIdentifier] = [:]
        private var vertexSamplers: [Int: ObjectIdentifier] = [:]
        private var fragmentSamplers: [Int: ObjectIdentifier] = [:]

        mutating func bindPipeline(_ value: MTLRenderPipelineState, encoder: MTLRenderCommandEncoder) {
            let id = ObjectIdentifier(value)
            if pipeline == id { skips += 1; return }
            encoder.setRenderPipelineState(value); pipeline = id; changes += 1
        }
        mutating func bindDepth(_ value: MTLDepthStencilState, encoder: MTLRenderCommandEncoder) {
            let id = ObjectIdentifier(value)
            if depth == id { skips += 1; return }
            encoder.setDepthStencilState(value); depth = id; changes += 1
        }
        mutating func bindStencil(_ value: UInt32, encoder: MTLRenderCommandEncoder) {
            if stencil == value { skips += 1; return }
            encoder.setStencilReferenceValue(value); stencil = value; changes += 1
        }
        mutating func bindCull(_ value: MTLCullMode, encoder: MTLRenderCommandEncoder) {
            if cull == value { skips += 1; return }
            encoder.setCullMode(value); cull = value; changes += 1
        }
        mutating func bindGeometry(_ value: MTLBuffer, encoder: MTLRenderCommandEncoder) {
            let id = ObjectIdentifier(value)
            if vertexBuffer == id { skips += 1; return }
            encoder.setVertexBuffer(value, offset: 0, index: 30)
            vertexBuffer = id; vertexBytes.removeValue(forKey: 30); changes += 1
        }
        mutating func bindUniform(_ bytes: Data, vertex: Bool, index: Int, encoder: MTLRenderCommandEncoder) {
            if (vertex ? vertexBytes[index] : fragmentBytes[index]) == bytes { skips += 1; return }
            bytes.withUnsafeBytes { raw in
                guard let address = raw.baseAddress else { return }
                if vertex { encoder.setVertexBytes(address, length: bytes.count, index: index) }
                else { encoder.setFragmentBytes(address, length: bytes.count, index: index) }
            }
            if vertex {
                vertexBytes[index] = bytes
                if index == 30 { vertexBuffer = nil }
            } else { fragmentBytes[index] = bytes }
            changes += 1
        }
        mutating func bindTexture(_ value: ResolvedTexture, encoder: MTLRenderCommandEncoder) {
            let plan = value.plan, asset = value.asset
            let texture = ObjectIdentifier(asset.texture), sampler = ObjectIdentifier(asset.sampler)
            if (plan.vertex ? vertexTextures[plan.index] : fragmentTextures[plan.index]) == texture { skips += 1 }
            else {
                if plan.vertex {
                    encoder.setVertexTexture(asset.texture, index: plan.index); vertexTextures[plan.index] = texture
                } else {
                    encoder.setFragmentTexture(asset.texture, index: plan.index); fragmentTextures[plan.index] = texture
                }
                changes += 1
            }
            if (plan.vertex ? vertexSamplers[plan.samplerIndex] : fragmentSamplers[plan.samplerIndex]) == sampler { skips += 1 }
            else {
                if plan.vertex {
                    encoder.setVertexSamplerState(asset.sampler, index: plan.samplerIndex); vertexSamplers[plan.samplerIndex] = sampler
                } else {
                    encoder.setFragmentSamplerState(asset.sampler, index: plan.samplerIndex); fragmentSamplers[plan.samplerIndex] = sampler
                }
                changes += 1
            }
        }
    }
    private struct PendingTexture {
        let id: String
        let aliases: [String]
        let info: [String: Any]
    }

    enum Failure: Error, CustomStringConvertible {
        case message(String)
        var description: String {
            switch self { case .message(let value): return value }
        }
    }

    private let queue: MTLCommandQueue
    private let root: URL
    private var sceneColorMode: SceneColorMode
    private var sceneColorTexture: MTLTexture?
    private var uiComposite: HUDSourceUIComposite?
    private var sceneColorPixelFormat: MTLPixelFormat {
        sceneColorMode == .sourceRGBHDR ? .rg11b10Float : colorPixelFormat
    }
    private let shaders: [String: Shader]
    private struct RuntimeSelection: Decodable {
        var schema: Int
        var profile: String
        var material_indices: [Int]
        var texture_indices: [Int]
        var source_text: Bool?
        var materials_sha256: String?
    }
    private let runtimeSelection: RuntimeSelection?
    /// CPU metadata only. Never retains a device, program, texture, geometry
    /// buffer, view or drawable. A replacement inventory replaces this slot.
    private final class MetadataCatalog {
        let key: String
        let shaders: [String: Shader]
        let objects: [String: Any]
        let sourceBytes: Int
        let materialInputs: [MaterialInputs]
        let fragmentsWriteDepth: Bool
        let bundledRoot: String?
        init(key: String, shaders: [String: Shader], objects: [String: Any], sourceBytes: Int, materialInputs: [MaterialInputs], fragmentsWriteDepth: Bool, bundledRoot: String?) {
            self.key = key; self.shaders = shaders; self.objects = objects; self.sourceBytes = sourceBytes
            self.materialInputs = materialInputs
            self.fragmentsWriteDepth = fragmentsWriteDepth
            self.bundledRoot = bundledRoot
        }
    }
    private static let metadataCondition = NSCondition()
    private static var retainedMetadata: MetadataCatalog?
    private static var metadataLoading = false
    private static var metadataPrewarmScheduled = false
    private let metadata: MetadataCatalog?
    private(set) var initializationPhaseMilliseconds: [String: Double] = [:]

    private static let shaderSpecifications: [(String, String)] = {
        var result = [("fx", "fx-shader.json"), ("image", "image-shader.json"), ("imageStencil", "image-stencil-shader.json"),
            ("imageMainFX", "image-mainfx-shader.json"), ("imageDissolveFX", "image-dissolvefx-shader.json"),
            ("imageAlphaClip", "image-alphaclip-shader.json"), ("imageClipRect", "image-cliprect-shader.json"),
            ("imageClipRectAlpha", "image-cliprect-alphaclip-shader.json"), ("font", "font-shader.json"),
            ("fontUnderlay", "font-underlay-shader.json"), ("imageWorld", "image-world-shader.json")]
        for program in 12...17 { result.append(("map\(program)", "map-\(program)-shader.json")) }
        for program in [12, 13] { result.append(("fx\(program)", "fx-\(program)-shader.json")) }
        for (key, stem) in [("imageMainFX", "image-mainfx"), ("imageDissolveFX", "image-dissolvefx"),
                            ("imageWorld", "image-world"), ("imageStencil", "image-stencil"),
                            ("font", "font"), ("fontUnderlay", "font-underlay")] {
            for (suffix, file) in [("AlphaClip", "alphaclip"), ("ClipRect", "cliprect"), ("ClipRectAlpha", "cliprect-alphaclip")] {
                result.append((key + suffix, stem + "-" + file + "-shader.json"))
            }
        }
        for (key, stem) in [("imageSoftMask", "image-softmask"), ("imageMainFXSoftMask", "image-mainfx-softmask"),
                            ("imageDissolveFXSoftMask", "image-dissolvefx-softmask"), ("imageWorldSoftMask", "image-world-softmask"),
                            ("imageStencilSoftMask", "image-stencil-softmask"), ("fontSoftMask", "font-softmask"),
                            ("fontUnderlaySoftMask", "font-underlay-softmask")] {
            for (suffix, file) in [("", ""), ("AlphaClip", "-alphaclip"), ("ClipRect", "-cliprect"), ("ClipRectAlpha", "-cliprect-alphaclip")] {
                result.append((key + suffix, stem + file + "-shader.json"))
            }
        }
        return result
    }()

    private static func metadataKey(root: URL, files: [String]) throws -> String {
        let inventory = try Data(contentsOf: root.appendingPathComponent("runtime-inventory.json"))
        var identity = root.resolvingSymlinksInPath().standardizedFileURL.path
            + "/" + SHA256.hash(data: inventory).map { String(format: "%02x", $0) }.joined()
        // Inventory identity binds the packaged catalog; file identity also
        // rejects an in-place edit/replacement during local preview or loading.
        for name in files.sorted() {
            let attrs = try FileManager.default.attributesOfItem(atPath: root.appendingPathComponent(name).path)
            identity += "/\(name):\(attrs[.size] ?? 0):\(attrs[.systemFileNumber] ?? 0):\((attrs[.modificationDate] as? Date)?.timeIntervalSince1970 ?? 0)"
        }
        return identity
    }

    private static func desktopMetadata(root: URL) throws -> MetadataCatalog {
        // The running .app's resource catalog is immutable for its process
        // lifetime; updates relaunch the app. Validate it once during preload.
        // External preview/probe trees keep full per-request file validation.
        let path = root.standardizedFileURL.path
        let bundledRoot = Bundle.main.bundleURL.pathExtension == "app"
            && Bundle.main.resourceURL?.appendingPathComponent("WatchSource").standardizedFileURL.path == path ? path : nil
        if let bundledRoot {
            metadataCondition.lock()
            let cached = retainedMetadata.flatMap { $0.bundledRoot == bundledRoot ? $0 : nil }
            metadataCondition.unlock()
            if let cached { return cached }
        }
        let material = FileManager.default.fileExists(atPath: root.appendingPathComponent("runtime-materials.json").path)
            ? "runtime-materials.json" : "materials.json"
        let objectFiles = ["render-color-policy.json", "textures.json", material]
            + ["Equipring", "watchline", "Plane", "Cylinder"].map { "Meshes/" + $0 + ".json" }
        let fragmentFiles = try FileManager.default.contentsOfDirectory(atPath: root.appendingPathComponent("Shaders").path)
            .filter { $0.hasSuffix(".fragment.metal") }.map { "Shaders/" + $0 }
        let files = objectFiles + shaderSpecifications.map(\.1) + ["runtime-selection.json"] + fragmentFiles
        let key = try metadataKey(root: root, files: files)
        metadataCondition.lock()
        if metadataLoading {
            while metadataLoading { metadataCondition.wait() }
            metadataCondition.unlock()
            // The resource identity could have changed while another load ran.
            return try desktopMetadata(root: root)
        }
        if let cached = retainedMetadata, cached.key == key { metadataCondition.unlock(); return cached }
        metadataLoading = true; metadataCondition.unlock()
        do {
            var sourceBytes = 0
            func read(_ name: String) throws -> Data {
                let data = try HUDSourceResourceData.read(root.appendingPathComponent(name))
                sourceBytes += data.count
                guard sourceBytes <= 16 * 1024 * 1024 else { throw Failure.message("Watch metadata exceeds bounded catalog") }
                return data
            }
            var shaders: [String: Shader] = [:]
            for (name, file) in shaderSpecifications { shaders[name] = try JSONDecoder().decode(Shader.self, from: read(file)) }
            var objects: [String: Any] = [:]
            for file in objectFiles { objects[file] = try JSONSerialization.jsonObject(with: read(file)) }
            let selection = try JSONDecoder().decode(RuntimeSelection.self, from: read("runtime-selection.json"))
            let records = try materialRecords(object: objects[material]!, compact: material == "runtime-materials.json", selection: selection)
            let inputs = try records.map { try parseMaterialInputs(record: $0) }
            let fragmentsWriteDepth = try Self.fragmentsWriteDepth(shaders: shaders, root: root)
            guard try metadataKey(root: root, files: files) == key else { throw Failure.message("Watch metadata changed while loading") }
            let catalog = MetadataCatalog(key: key, shaders: shaders, objects: objects, sourceBytes: sourceBytes,
                materialInputs: inputs, fragmentsWriteDepth: fragmentsWriteDepth, bundledRoot: bundledRoot)
            metadataCondition.lock(); retainedMetadata = catalog; metadataLoading = false
            metadataCondition.broadcast(); metadataCondition.unlock()
            return catalog
        } catch {
            metadataCondition.lock(); metadataLoading = false; metadataCondition.broadcast(); metadataCondition.unlock()
            throw error
        }
    }

    /// Synchronous CPU-only readiness seam for launch preparation and timed
    /// verification. Original/reference resources intentionally have no cache.
    static func prepareDesktopMetadataIfNeeded(resourceRoot: URL? = nil) throws {
        guard let root = resourceRoot ?? HUDResources.url(for: "WatchSource"),
              FileManager.default.fileExists(atPath: root.appendingPathComponent("runtime-selection.json").path) else { return }
        _ = try desktopMetadata(root: root)
    }

    static func prewarmDesktopResources() {
        guard let root = HUDResources.url(for: "WatchSource"),
              FileManager.default.fileExists(atPath: root.appendingPathComponent("runtime-selection.json").path) else { return }
        metadataCondition.lock()
        guard !metadataPrewarmScheduled else { metadataCondition.unlock(); return }
        metadataPrewarmScheduled = true; metadataCondition.unlock()
        DispatchQueue.global(qos: .utility).async {
            _ = try? desktopMetadata(root: root)
            _ = try? prepareDesktopProgramsIfNeeded(resourceRoot: root)
            metadataCondition.lock(); metadataPrewarmScheduled = false; metadataCondition.unlock()
        }
    }

    #if HUD_SOURCE_RENDER_PREVIEW
    static func metadataIdentityForVerification(root: URL) throws -> ObjectIdentifier {
        ObjectIdentifier(try desktopMetadata(root: root))
    }
    static func verifyMetadataValuesForVerification(root: URL) throws -> [String: Int] {
        let catalog = try desktopMetadata(root: root)
        for (name, cached) in catalog.objects {
            let original = try JSONSerialization.jsonObject(with: HUDSourceResourceData.read(root.appendingPathComponent(name)))
            guard let object = cached as? NSObject, object.isEqual(original) else {
                throw Failure.message("Cached source metadata differs: " + name)
            }
        }
        guard catalog.shaders.count == shaderSpecifications.count else { throw Failure.message("Duplicate/missing shader metadata") }
        let compact = catalog.objects["runtime-materials.json"] != nil
        let fresh = try JSONSerialization.jsonObject(with: HUDSourceResourceData.read(root.appendingPathComponent(compact ? "runtime-materials.json" : "materials.json")))
        let selection = try JSONDecoder().decode(RuntimeSelection.self, from: HUDSourceResourceData.read(root.appendingPathComponent("runtime-selection.json")))
        let inputs = try materialRecords(object: fresh, compact: compact, selection: selection).map { try parseMaterialInputs(record: $0) }
        guard inputs.count == catalog.materialInputs.count else { throw Failure.message("Cached material count differs") }
        for (fresh, cached) in zip(inputs, catalog.materialInputs) {
            guard fresh.name == cached.name, fresh.id == cached.id, fresh.values == cached.values,
                  fresh.textureIDs == cached.textureIDs, fresh.shaderID == cached.shaderID,
                  fresh.keywords == cached.keywords, fresh.clipVariants == cached.clipVariants,
                  fresh.propertyTypes.count == cached.propertyTypes.count,
                  fresh.propertyTypes.allSatisfy({ key, value in
                      guard let other = cached.propertyTypes[key] else { return false }
                      return value.type == other.type && value.flags == other.flags
                  }) else { throw Failure.message("Cached material values differ: " + fresh.name) }
        }
        return ["catalogs": 1, "objects": catalog.objects.count, "shaders": catalog.shaders.count,
                "materials": inputs.count, "sourceBytes": catalog.sourceBytes,
                "fragmentsWriteDepth": catalog.fragmentsWriteDepth ? 1 : 0]
    }
    #endif
    // A color-attachment change needs new pipelines, not new compilation of
    // the same source functions. Only used variants compile, once per view.
    private var shaderFunctions: [String: (MTLFunction, MTLFunction)] = [:]
    private var shaderLibraries: [String: MTLLibrary] = [:]
    private struct SourcePipelineKey: Hashable {
        var shader: String
        var vertexLayout: [Int]
        var attachmentState: [UInt]
    }
    /// Only immutable GPU programs survive a closed overlay. A single exact
    /// packaged resource version/device is retained, with fixed entry caps;
    /// textures, geometry, drawables and scene documents never enter here.
    private final class ProgramCache {
        let key: String
        let lock = NSLock()
        var libraries: [String: MTLLibrary] = [:]
        var functions: [String: (MTLFunction, MTLFunction)] = [:]
        var pipelines: [SourcePipelineKey: (MTLRenderPipelineState, MTLRenderPipelineReflection)] = [:]
        var prewarmStarted = false
        var prewarmCompleted = false
        var prewarmShaderCount = 0
        var prewarmLibraryCompilations = 0
        init(key: String) { self.key = key }
        func access<T>(_ body: (ProgramCache) -> T) -> T {
            lock.lock(); defer { lock.unlock() }
            return body(self)
        }
    }
    private static let programCacheLock = NSLock()
    private static var retainedPrograms: ProgramCache?
    private let programCache: ProgramCache?
    private static func programs(root: URL, device: MTLDevice) throws -> ProgramCache {
        let inventory = try Data(contentsOf: root.appendingPathComponent("runtime-inventory.json"))
        let fingerprint = SHA256.hash(data: inventory).map { String(format: "%02x", $0) }.joined()
        let key = String(device.registryID) + "/" + fingerprint
        programCacheLock.lock(); defer { programCacheLock.unlock() }
        if let retainedPrograms, retainedPrograms.key == key { return retainedPrograms }
        let cache = ProgramCache(key: key)
        retainedPrograms = cache
        return cache
    }

    /// Prepare only immutable programs referenced by the desktop's drawable
    /// components. No view, command queue, pipeline, texture or geometry is
    /// constructed. Required clip/mask variants share the same fixed budget;
    /// any omitted programs stay lazy.
    @discardableResult
    static func prepareDesktopProgramsIfNeeded(resourceRoot: URL? = nil) throws -> [String: Int] {
        guard let root = resourceRoot ?? HUDResources.url(for: "WatchSource"),
              FileManager.default.fileExists(atPath: root.appendingPathComponent("runtime-selection.json").path) else {
            return programCacheStatisticsForVerification()
        }
        let catalog = try desktopMetadata(root: root)
        let document = try HUDSourceWatchDocument.desktop(resourceRoot: root.appendingPathComponent("Scene"))
        let keys = try desktopProgramShaderKeys(catalog: catalog, document: document, root: root)
        guard let device = MTLCreateSystemDefaultDevice() else { throw Failure.message("Metal device unavailable") }
        let cache = try programs(root: root, device: device)
        let shouldPrepare = cache.access { cache -> Bool in
            guard !cache.prewarmStarted else { return false }
            cache.prewarmStarted = true
            // A changed catalog cannot turn launch preparation into an
            // unbounded compilation job; the normal renderer handles the rest.
            cache.prewarmShaderCount = min(keys.count, 8)
            return true
        }
        if shouldPrepare {
            for key in keys.prefix(8) {
                guard let shader = catalog.shaders[key] else { throw Failure.message("Unmapped desktop shader: " + key) }
                _ = try programFunctions(for: key, shader: shader, root: root, device: device, cache: cache) {
                    cache.access { $0.prewarmLibraryCompilations += 1 }
                }
            }
            cache.access { $0.prewarmCompleted = true }
        }
        return programCacheStatisticsForVerification()
    }

    static func programCacheStatisticsForVerification() -> [String: Int] {
        programCacheLock.lock(); let cache = retainedPrograms; programCacheLock.unlock()
        guard let cache else { return ["libraries": 0, "functions": 0, "pipelines": 0,
            "prewarmStarted": 0, "prewarmCompleted": 0, "prewarmShaders": 0, "prewarmLibraryCompilations": 0] }
        return cache.access { ["libraries": $0.libraries.count, "functions": $0.functions.count,
            "pipelines": $0.pipelines.count, "prewarmStarted": $0.prewarmStarted ? 1 : 0,
            "prewarmCompleted": $0.prewarmCompleted ? 1 : 0, "prewarmShaders": $0.prewarmShaderCount,
            "prewarmLibraryCompilations": $0.prewarmLibraryCompilations] }
    }

    private static func desktopProgramShaderKeys(catalog: MetadataCatalog,
                                                 document: HUDSourceWatchDocument, root: URL) throws -> [String] {
        let hidden = document.desktopHiddenNodeIDs
        let sorting = HUDSourceCanvasSorting(scene: document.scene, components: document.components).resolve(panelBase: 0)
        var materialIDs = Set<String>()
        var variantRequests: [String: Set<String>] = [:]
        for node in document.scene.nodes {
            var ancestor: HUDSourceID? = node.id
            var excluded = false
            var searchesClip = true, hasClip = false
            var nearestSoftMaskEnabled: Bool?
            while let id = ancestor {
                if hidden.contains(id) { excluded = true; break }
                // Match FrameBuilder's Canvas sorting boundaries and its
                // nearest-mask rule, including a disabled nearest soft mask.
                if searchesClip {
                    hasClip = hasClip || document.component("RectMask2D", on: id) != nil
                    if sorting[id]?.startsSortingBoundary == true { searchesClip = false }
                }
                if nearestSoftMaskEnabled == nil,
                   let mask = document.components[id]?.first(where: { $0.kind == "UISoftMask" }) {
                    nearestSoftMaskEnabled = mask.enabled
                }
                ancestor = document.scene.node(id)?.parentID
            }
            guard !excluded else { continue }
            let usesSoftMask = document.component("UISoftMaskable", on: node.id) != nil && nearestSoftMaskEnabled == true
            let variant = usesSoftMask ? (hasClip ? "softClip" : "soft") : (hasClip ? "clip" : nil)
            func includeGraphicMaterial(_ id: String) {
                materialIDs.insert(id)
                if let variant { variantRequests[id, default: []].insert(variant) }
            }
            for component in document.components[node.id] ?? [] where component.enabled {
                if ["UIImage", "Image", "UIRawImage", "RawImage"].contains(component.kind) {
                    includeGraphicMaterial(component["m_Material"].targetID?.rawValue ?? "__ui_default")
                    if ["UIRawImage", "RawImage"].contains(component.kind),
                       let animation = document.component("UIGraphicAnimation", on: node.id),
                       let material = animation["_material"].targetID {
                        includeGraphicMaterial(material.rawValue)
                    }
                } else if component.kind == "MeshRenderer" {
                    for material in component["m_Materials"].array {
                        if let id = material.targetID { materialIDs.insert(id.rawValue) }
                    }
                }
            }
        }
        let selection = try JSONDecoder().decode(RuntimeSelection.self,
            from: HUDSourceResourceData.read(root.appendingPathComponent("runtime-selection.json")))
        let compact = catalog.objects["runtime-materials.json"] != nil
        let records = try materialRecords(object: catalog.objects[compact ? "runtime-materials.json" : "materials.json"]!,
            compact: compact, selection: selection)
        var variantMaterialIDs = Set<String>()
        for inputs in catalog.materialInputs {
            let flags = (variantRequests[inputs.name] ?? []).union(inputs.id.flatMap { variantRequests[$0] } ?? [])
            for flag in flags {
                if let id = inputs.clipVariants?[flag] { variantMaterialIDs.insert(id) }
            }
        }
        var keys = Set<String>(), variantKeys = Set<String>()
        for (record, inputs) in zip(records, catalog.materialInputs)
            where materialIDs.contains(inputs.name) || inputs.id.map(materialIDs.contains) == true
                || variantMaterialIDs.contains(inputs.name) || inputs.id.map(variantMaterialIDs.contains) == true {
            let isVariant = variantMaterialIDs.contains(inputs.name) || inputs.id.map(variantMaterialIDs.contains) == true
            let isMap = inputs.shaderID == "505394952752169778"
            for pass in record["static_pass_states"] as? [[String: Any]] ?? [] {
                if pass["disabled_in_serialized_material"] as? Bool == true { continue }
                guard let name = pass["name"] as? String else { throw Failure.message("Incomplete source pass") }
                guard name == "Default" || name == "Default-Stencil-Alpha-Blend" || isMap && name == "ForwardOnly" else { continue }
                let key = shaderKey(inputs: inputs, passName: name)
                keys.insert(key)
                if isVariant { variantKeys.insert(key) }
            }
        }
        // Visible clipped buttons must not fall beyond the preparation cap
        // merely because their shader names sort after unclipped variants.
        return variantKeys.sorted() + keys.subtracting(variantKeys).sorted()
    }

    #if HUD_SOURCE_RENDER_PREVIEW
    /// CPU-only selection seam; does not construct a Metal device or view.
    static func desktopPrewarmShaderKeysForVerification(resourceRoot: URL) throws -> [String] {
        let catalog = try desktopMetadata(root: resourceRoot)
        let document = try HUDSourceWatchDocument.desktop(resourceRoot: resourceRoot.appendingPathComponent("Scene"))
        return Array(try desktopProgramShaderKeys(catalog: catalog, document: document, root: resourceRoot).prefix(8))
    }
    #endif

    private static func programFunctions(for key: String, shader: Shader, root: URL,
                                         device: MTLDevice, cache: ProgramCache,
                                         didCompile: () -> Void) throws -> (MTLFunction, MTLFunction) {
        if let pair = cache.access({ $0.functions[key] }) { return pair }
        func function(_ stage: Stage?) throws -> MTLFunction {
            guard let stage else { throw Failure.message("Incomplete source shader interface") }
            let source = try String(contentsOf: root.appendingPathComponent(stage.file), encoding: .utf8)
            let library: MTLLibrary
            if let existing = cache.access({ $0.libraries[source] }) { library = existing }
            else {
                // Never hold the cache lock across Metal compilation. An early
                // hotkey can compile its needed program immediately; a rare
                // race may compile twice but retains just one bounded entry.
                let compiled = try device.makeLibrary(source: source, options: nil)
                didCompile()
                library = cache.access {
                    if let existing = $0.libraries[source] { return existing }
                    if $0.libraries.count < 32 { $0.libraries[source] = compiled }
                    return compiled
                }
            }
            guard let function = library.makeFunction(name: stage.function) else {
                throw Failure.message("Translated source entry point unavailable: " + key)
            }
            return function
        }
        let pair = try (function(shader.stages["vertex"]), function(shader.stages["fragment"]))
        return cache.access {
            if let existing = $0.functions[key] { return existing }
            if $0.functions.count < 32 { $0.functions[key] = pair }
            return pair
        }
    }
    private var sourcePipelines: [SourcePipelineKey: (MTLRenderPipelineState, MTLRenderPipelineReflection)] = [:]
    private var desktopAccentLinear: SIMD3<Float>?
    private var uniformCache: [UniformCacheKey: UniformCacheEntry] = [:]
    private var preparedUniformBatches: [PreparedBatchUniforms?] = []
    private var sharedPreparedUniforms: [SharedUniformKey: PreparedUniformCell] = [:]
    private var preparedUniformsEnabled = true
    private(set) var preparedUniformHitCount = 0
    private(set) var preparedUniformResolutionCount = 0
    // Applied only to the packaged desktop selection in draw(in:). The
    // original source/reference renderer retains its unmerged draw stream.
    private var adjacentBatchMergingEnabled = true
    private var mergedGeometrySlots: [MergedGeometry?] = []
    private var mergeSafePasses: [ObjectIdentifier: Bool] = [:]
    private(set) var sourcePassDrawCount = 0
    private(set) var encodedPassDrawCount = 0
    private(set) var mergedBatchCount = 0
    private(set) var mergedGeometryReuseCount = 0
    #if HUD_SOURCE_RENDER_PREVIEW
    var verifyPreparedUniformBytesForVerification = false
    private(set) var verifiedPreparedUniformByteCount = 0
    var verifyMergedGeometryBytesForVerification = false
    private(set) var verifiedMergedGeometryByteCount = 0
    #endif
    private var uniformCamera: Camera?
    private var uniformDrawableSize = CGSize.zero
    private var uniformCameraVersion: UInt64 = 0
    private var uniformTimeVersion: UInt64 = 0
    private(set) var uniformCacheHitCount = 0
    private(set) var uniformEncodeCount = 0
    private(set) var geometryBufferReuseCount = 0
    private(set) var geometryBufferAllocationCount = 0
    private(set) var resourceGeneration: UInt64 = 0
    private(set) var compiledSourcePipelineCount = 0
    private(set) var compiledSourceLibraryCount = 0
    private(set) var encoderBindingChangeCount = 0
    private(set) var encoderBindingSkipCount = 0
    var resourceStatisticsForVerification: [String: Int] {
        var textures: [ObjectIdentifier: MTLTexture] = [:]
        for asset in textureAssets.values { textures[ObjectIdentifier(asset.texture)] = asset.texture }
        return ["compiledSourcePipelines": compiledSourcePipelineCount,
                "compiledSourceLibraries": compiledSourceLibraryCount,
                "uniformCacheHits": uniformCacheHitCount, "uniformEncodes": uniformEncodeCount,
                "preparedUniformHits": preparedUniformHitCount, "preparedUniformResolutions": preparedUniformResolutionCount,
                "sourcePassDraws": sourcePassDrawCount, "encodedPassDraws": encodedPassDrawCount,
                "mergedBatches": mergedBatchCount, "mergedGeometryReuses": mergedGeometryReuseCount,
                "geometryBufferReuses": geometryBufferReuseCount, "geometryBufferAllocations": geometryBufferAllocationCount,
                "retainedProgramLibraries": programCache?.access { $0.libraries.count } ?? 0,
                "retainedProgramPipelines": programCache?.access { $0.pipelines.count } ?? 0,
                "depthStencilBytes": depthStencilTexture?.allocatedSize ?? 0,
                "stencilOnly": depthStencilPixelFormat == .stencil8 ? 1 : 0,
                "sourceTextureLoads": sourceTextureLoadCount,
                "encoderBindingChanges": encoderBindingChangeCount, "encoderBindingSkips": encoderBindingSkipCount,
                "deferredSourceTextures": Set(pendingTextures.values.map(\.id)).count,
                "textureCount": textures.count,
                "textureBytes": textures.values.reduce(0) { $0 + $1.allocatedSize }]
    }
    /// On-demand fixture proof of the actual uploaded positions, UVs, normals,
    /// colors and indices. No recurring readback, copy, or history is retained.
    func geometryFingerprintForVerification(meshNames: Set<String>) throws -> [String: String] {
        var result: [String: String] = [:]
        for name in meshNames {
            guard let geometry = geometries[name], geometry.vertices.storageMode == .shared,
                  geometry.indices.storageMode == .shared else {
                throw Failure.message("Shared source geometry unavailable for fingerprint: " + name)
            }
            let vertexBytes = geometry.originalVertices.count * MemoryLayout<Vertex>.stride
            let indexBytes = geometry.indexCount * MemoryLayout<UInt32>.stride
            guard vertexBytes <= geometry.vertices.length, indexBytes <= geometry.indices.length else {
                throw Failure.message("Source geometry fingerprint exceeds uploaded capacity: " + name)
            }
            var hash = SHA256()
            hash.update(bufferPointer: UnsafeRawBufferPointer(start: geometry.vertices.contents(), count: vertexBytes))
            hash.update(bufferPointer: UnsafeRawBufferPointer(start: geometry.indices.contents(), count: indexBytes))
            let digest = hash.finalize().map { String(format: "%02x", $0) }.joined()
            result[name] = String(vertexBytes) + "/" + String(indexBytes) + "/" + digest
        }
        return result
    }
    private var geometries: [String: Geometry] = [:]
    private var materials: [String: Material] = [:]
    private var clipMaterialKeys: [String: [String: String]] = [:]
    private var materialPropertyTypes: [String: [String: (type: Int, flags: Int)]] = [:]
    private var textureAssets: [String: TextureAsset] = [:]
    private var pendingTextures: [String: PendingTexture] = [:]
    private var textureFailures: [String: String] = [:]
    private var sourceTextureLoadCount = 0
    private var camera: Camera?
    private var batches: [Batch] = []
    private var lastDrawable: CAMetalDrawable?
    private var lastRenderCommand: MTLCommandBuffer?
    private var measuringFirstDrawable = false
    private var awaitingFirstPresentation = false
    private(set) var firstCompletedFrameTimestampForVerification: CFTimeInterval?
    private(set) var firstPresentedFrameTimestampForVerification: CFTimeInterval?
    private(set) var submittedFrameGeneration: UInt64 = 0
    private(set) var renderedFrameGeneration: UInt64?
    private(set) var drawableReadbackReport: HUDSourceDrawableReadback.Report?
    private(set) var drawableReadbackBGRA: Data?
    private struct TintedVertices {
        var color: SIMD4<Float>
        var buffer: MTLBuffer
        var geometryRevision: UInt64
    }
    private var tintedVertices: [String: TintedVertices] = [:]
    private struct StencilKey: Hashable { var pass: String; var state: StencilState }
    private var stencilStates: [StencilKey: MTLDepthStencilState] = [:]
    private var colorPipelines: [String: MTLRenderPipelineState] = [:]
    private var requiresCombinedDepth = false
    private struct MaterialSet {
        var materials: [String: Material]
        var propertyTypes: [String: [String: (type: Int, flags: Int)]]
        var clipKeys: [String: [String: String]]
        var abi: [String: ConstantBufferABIRecord]
        var depthFormat: MTLPixelFormat
    }
    private var materialSets: [SceneColorMode: MaterialSet] = [:]
    private(set) var diagnostics: [String] = []

    struct ConstantBufferABIRecord: Encodable {
        var file: String
        var stage: String
        var name: String
        var index: Int
        var sourceSize: Int
        var reflectedSize: Int?
        var uploadedSize: Int
    }
    private var constantBufferABIRecords: [String: ConstantBufferABIRecord] = [:]
    var constantBufferABI: [ConstantBufferABIRecord] {
        constantBufferABIRecords.keys.sorted().compactMap { constantBufferABIRecords[$0] }
    }

    override var isOpaque: Bool { false }
    override func hitTest(_ point: NSPoint) -> NSView? { nil }

    init(frame: CGRect, resourceRoot: URL? = nil, sceneColorMode: SceneColorMode = .directLDR,
         recordStartupTimings: Bool = false) throws {
        let started = recordStartupTimings ? CACurrentMediaTime() : 0
        var previous = started, timings: [String: Double] = [:]
        func mark(_ name: String) {
            guard recordStartupTimings else { return }
            let current = CACurrentMediaTime(); timings[name] = (current - previous) * 1000; previous = current
        }
        guard let device = MTLCreateSystemDefaultDevice(), let queue = device.makeCommandQueue() else {
            throw Failure.message("Metal device or command queue unavailable")
        }
        guard let root = resourceRoot ?? HUDResources.url(for: "WatchSource") else {
            throw Failure.message("WatchSource resource directory unavailable")
        }
        mark("deviceAndQueue")
        self.queue = queue
        self.root = root
        self.sceneColorMode = sceneColorMode
        let selectionURL = root.appendingPathComponent("runtime-selection.json")
        if FileManager.default.fileExists(atPath: selectionURL.path) {
            let selection = try JSONDecoder().decode(RuntimeSelection.self, from: HUDSourceResourceData.read(selectionURL))
            guard selection.schema == 1, selection.profile == "desktop-shell" else {
                throw Failure.message("Unknown Watch runtime resource selection")
            }
            runtimeSelection = selection
        } else { runtimeSelection = nil }
        programCache = runtimeSelection == nil ? nil : try Self.programs(root: root, device: device)
        metadata = runtimeSelection == nil ? nil : try Self.desktopMetadata(root: root)
        let colorPolicy = try (metadata?.objects["render-color-policy.json"]
            ?? JSONSerialization.jsonObject(with: HUDSourceResourceData.read(root.appendingPathComponent("render-color-policy.json")))) as? [String: Any]
        guard (colorPolicy?["serialized_color_space"] as? Int) == 1 else {
            throw Failure.message("Original linear project color-space evidence unavailable")
        }
        if let metadata { shaders = metadata.shaders }
        else {
            var catalog: [String: Shader] = [:]
            for (key, file) in Self.shaderSpecifications {
                catalog[key] = try JSONDecoder().decode(Shader.self,
                    from: HUDSourceResourceData.read(root.appendingPathComponent(file)))
            }
            shaders = catalog
        }
        mark("catalog")
        super.init(frame: frame, device: device)
        // Source PlayerSettings is Linear. The display attachment performs its
        // sRGB transfer. HDR mode renders into the original RGB format first.
        colorPixelFormat = .bgra8Unorm_srgb
        depthStencilPixelFormat = .depth32Float_stencil8
        clearColor = MTLClearColorMake(0, 0, 0, 0)
        clearDepth = 1
        clearStencil = 0
        sampleCount = 1
        framebufferOnly = false
        wantsLayer = true
        layer?.isOpaque = false
        layer?.backgroundColor = NSColor.clear.cgColor
        (layer as? CAMetalLayer)?.colorspace = CGColorSpace(name: CGColorSpace.sRGB)
        if runtimeSelection != nil { (layer as? CAMetalLayer)?.maximumDrawableCount = 2 }
        isPaused = true
        enableSetNeedsDisplay = true
        delegate = self
        if sceneColorMode == .sourceRGBHDR {
            uiComposite = try HUDSourceUIComposite(device: device, resourceRoot: root, outputPixelFormat: colorPixelFormat)
        }
        mark("view")
        try loadGeometries(device: device)
        mark("geometry")
        try loadTextures(device: device)
        if runtimeSelection?.source_text != false { try loadFontTextures() }
        mark("textureDescriptors")
        let materialTimings = try loadMaterials(device: device, recordStartupTimings: recordStartupTimings)
        mark("materials")
        timings.merge(materialTimings) { _, latest in latest }
        if recordStartupTimings {
            timings["total"] = (CACurrentMediaTime() - started) * 1000
            initializationPhaseMilliseconds = timings
        }
    }

    required init(coder: NSCoder) { fatalError("Use init(frame:resourceRoot:)") }

    /// Opt-in desktop presentation. Source/reference fixtures leave this nil.
    /// Only existing yellow color channels change; no extra render target,
    /// shader pass, texture copy or palette-history cache is introduced.
    func configureDesktopAccent(_ color: NSColor?) {
        let next: SIMD3<Float>?
        if let color = color?.usingColorSpace(.sRGB) {
            next = SIMD3(Self.gammaToLinear(Float(color.redComponent)),
                         Self.gammaToLinear(Float(color.greenComponent)),
                         Self.gammaToLinear(Float(color.blueComponent)))
        } else { next = nil }
        guard next != desktopAccentLinear else { return }
        desktopAccentLinear = next
        resourceGeneration &+= 1
        uniformCache.removeAll(keepingCapacity: true)
        clearPreparedUniforms()
        needsDisplay = true
    }

    /// Used only by the isolated UI fixture to compare both paths in one
    /// optimized binary. Ordinary launches cannot change the renderer policy.
    func setPreparedUniformsEnabledForVerification(_ enabled: Bool) {
        guard ProcessInfo.processInfo.arguments.contains("--ui-test") else { return }
        preparedUniformsEnabled = enabled
        clearPreparedUniforms()
    }

    func setAdjacentBatchMergingEnabledForVerification(_ enabled: Bool) {
        guard ProcessInfo.processInfo.arguments.contains("--ui-test") else { return }
        adjacentBatchMergingEnabled = enabled
        mergedGeometrySlots.removeAll(); mergeSafePasses.removeAll()
        clearPreparedUniforms()
    }

    private func clearPreparedUniforms() {
        preparedUniformBatches.removeAll(keepingCapacity: true)
        sharedPreparedUniforms.removeAll(keepingCapacity: true)
        mergeSafePasses.removeAll(keepingCapacity: true)
    }

    /// Batches retain the original scene draw order. Missing material pipelines
    /// are reported explicitly, never replaced with the FX additive program.
    func submit(camera: Camera, batches: [Batch]) {
        self.camera = camera
        self.batches = batches
        submittedFrameGeneration &+= 1
        drawableReadbackReport = nil
        drawableReadbackBGRA = nil
        needsDisplay = true
    }

    /// Sprite geometry is built from original Unity sprite vertices/UVs or the
    /// exact runtime RectTransform. This accepts those values without fitting.
    func registerGeometry(named name: String, positions: [SIMD4<Float>], uv: [SIMD2<Float>],
                          colors: [SIMD4<Float>] = [], indices: [UInt32],
                          normals: [SIMD3<Float>] = [], uv1: [SIMD2<Float>] = []) throws {
        guard let device, !positions.isEmpty, positions.count == uv.count,
              colors.isEmpty || colors.count == positions.count,
              normals.isEmpty || normals.count == positions.count,
              uv1.isEmpty || uv1.count == positions.count,
              !indices.isEmpty, indices.count % 3 == 0,
              indices.allSatisfy({ Int($0) < positions.count }) else { throw Failure.message("Invalid registered source geometry: \(name)") }
        var vertices = positions.indices.map { Vertex(position: positions[$0], uv: uv[$0], color: colors.isEmpty ? SIMD4<Float>(repeating: 1) : colors[$0]) }
        for i in vertices.indices {
            if !normals.isEmpty { vertices[i].normal = SIMD4(normals[i].x, normals[i].y, normals[i].z, 0) }
            if !uv1.isEmpty { vertices[i].uv1 = uv1[i] }
        }
        let previous = geometries[name]
        let reusable = geometryBuffersAreIdle
        let vertexBuffer = try geometryBuffer(vertices, previous: previous?.vertices, reusable: reusable, device: device)
        let indexBuffer = try geometryBuffer(indices, previous: previous?.indices, reusable: reusable, device: device)
        geometries[name] = Geometry(vertices: vertexBuffer, indices: indexBuffer, indexCount: indices.count, originalVertices: vertices,
                                    hasTextChannels: !normals.isEmpty && !uv1.isEmpty, revision: (previous?.revision ?? 0) &+ 1)
        resourceGeneration &+= 1
        // An idle tint allocation can be refilled on draw. Its old geometry
        // revision prevents stale colors/positions from being reused as-is.
        if !reusable { tintedVertices.removeValue(forKey: name) }
    }

    private var geometryBuffersAreIdle: Bool {
        lastRenderCommand == nil || lastRenderCommand?.status == .completed
    }

    private func geometryBuffer<T>(_ values: [T], previous: MTLBuffer?, reusable: Bool,
                                   device: MTLDevice) throws -> MTLBuffer {
        try values.withUnsafeBufferPointer { elements in
            let bytes = UnsafeRawBufferPointer(elements)
            guard let address = bytes.baseAddress, !bytes.isEmpty else {
                throw Failure.message("Empty source geometry upload")
            }
            // Exact capacity keeps memory bounded by current geometry; there
            // is no historical pool or oversized high-water allocation.
            if reusable, let previous, previous.storageMode == .shared, previous.length == bytes.count {
                previous.contents().copyMemory(from: address, byteCount: bytes.count)
                geometryBufferReuseCount += 1
                return previous
            }
            guard let buffer = device.makeBuffer(bytes: address, length: bytes.count, options: .storageModeShared) else {
                throw Failure.message("Cannot allocate source geometry buffer")
            }
            geometryBufferAllocationCount += 1
            return buffer
        }
    }

    /// Exported atlas PNGs preserve source pixels; their original Unity UVs
    /// have a bottom-left origin. Never generate a replacement mip chain.
    func registerTexture(named name: String, url: URL, sRGB: Bool, filterMode: Int = 1,
                         wrapU: Int = 1, wrapV: Int = 1) throws {
        guard let device else { throw Failure.message("Metal device unavailable") }
        let texture = try MTKTextureLoader(device: device).newTexture(URL: url, options: [
            .SRGB: NSNumber(value: sRGB), .origin: MTKTextureLoader.Origin.bottomLeft.rawValue,
            .generateMipmaps: NSNumber(value: false), .textureUsage: NSNumber(value: MTLTextureUsage.shaderRead.rawValue)
        ])
        let sampler = MTLSamplerDescriptor()
        sampler.minFilter = filterMode == 0 ? .nearest : .linear
        sampler.magFilter = sampler.minFilter
        sampler.mipFilter = filterMode == 2 ? .linear : .nearest
        sampler.sAddressMode = Self.addressMode(wrapU)
        sampler.tAddressMode = Self.addressMode(wrapV)
        guard let state = device.makeSamplerState(descriptor: sampler) else { throw Failure.message("Cannot allocate registered source sampler") }
        textureAssets[name] = TextureAsset(texture: texture, sampler: state)
        resourceGeneration &+= 1
    }

    /// Inventory queries must not allocate GPU storage. Callers can register
    /// dynamic textures independently; actual bindings resolve pending assets.
    func containsTexture(named name: String) -> Bool { textureAssets[name] != nil || pendingTextures[name] != nil }

    /// A dynamic original RT binding (for example WatchBlur's captured scene).
    /// Its caller supplies the source filter/wrap contract explicitly.
    func registerTexture(named name: String, texture: MTLTexture, filterMode: Int,
                         wrapU: Int, wrapV: Int) throws {
        guard let device, texture.device.registryID == device.registryID,
              texture.width > 0, texture.height > 0 else {
            throw Failure.message("Invalid dynamic source texture: " + name)
        }
        let descriptor = MTLSamplerDescriptor()
        descriptor.minFilter = filterMode == 0 ? .nearest : .linear
        descriptor.magFilter = descriptor.minFilter
        descriptor.mipFilter = .notMipmapped
        descriptor.sAddressMode = Self.addressMode(wrapU)
        descriptor.tAddressMode = Self.addressMode(wrapV)
        guard let sampler = device.makeSamplerState(descriptor: descriptor) else {
            throw Failure.message("Cannot allocate dynamic source texture sampler")
        }
        textureAssets[name] = TextureAsset(texture: texture, sampler: sampler)
        resourceGeneration &+= 1
    }

    /// Enable the original target only after the caller has prepared a real
    /// backdrop. Preserve the working pipeline set if compilation fails.
    func enableSourceRGBHDR() throws {
        try setSceneColorMode(.sourceRGBHDR)
    }

    func disableSourceRGBHDR() throws {
        try setSceneColorMode(.directLDR)
    }

    private func setSceneColorMode(_ mode: SceneColorMode) throws {
        guard sceneColorMode != mode else { return }
        guard let device else { throw Failure.message("Metal device unavailable") }
        if mode == .sourceRGBHDR && uiComposite == nil {
            uiComposite = try HUDSourceUIComposite(device: device, resourceRoot: root, outputPixelFormat: colorPixelFormat)
        }
        let previousMode = sceneColorMode
        let previous = MaterialSet(materials: materials, propertyTypes: materialPropertyTypes,
            clipKeys: clipMaterialKeys, abi: constantBufferABIRecords, depthFormat: depthStencilPixelFormat)
        materialSets[previousMode] = previous
        let previousColors = colorPipelines, previousStencil = stencilStates
        sceneColorMode = mode
        resourceGeneration &+= 1
        uniformCache.removeAll(keepingCapacity: true)
        clearPreparedUniforms()
        colorPipelines.removeAll(); stencilStates.removeAll()
        if let cached = materialSets[mode] {
            materials = cached.materials; materialPropertyTypes = cached.propertyTypes
            clipMaterialKeys = cached.clipKeys; constantBufferABIRecords = cached.abi
            depthStencilPixelFormat = cached.depthFormat
            if mode == .directLDR { sceneColorTexture = nil }
            return
        }
        do {
            materials.removeAll(); materialPropertyTypes.removeAll(); clipMaterialKeys.removeAll()
            constantBufferABIRecords.removeAll()
            try loadMaterials(device: device)
            materialSets[mode] = MaterialSet(materials: materials, propertyTypes: materialPropertyTypes,
                clipKeys: clipMaterialKeys, abi: constantBufferABIRecords, depthFormat: depthStencilPixelFormat)
            if mode == .directLDR { sceneColorTexture = nil }
        } catch {
            sceneColorMode = previousMode
            materials = previous.materials; materialPropertyTypes = previous.propertyTypes
            clipMaterialKeys = previous.clipKeys; constantBufferABIRecords = previous.abi
            depthStencilPixelFormat = previous.depthFormat
            colorPipelines = previousColors; stencilStates = previousStencil
            throw error
        }
    }

    /// Select the original UIImage/TMP material with explicit Canvas masking
    /// keywords. Derived records preserve its saved properties and pass state.
    func materialKey(named base: String, clipRect: Bool, alphaClip: Bool, softMask: Bool = false) -> String? {
        guard materials[base] != nil else { return nil }
        guard clipRect || alphaClip || softMask else { return base }
        let flag: String
        if softMask { flag = clipRect ? (alphaClip ? "softClipAlpha" : "softClip") : (alphaClip ? "softAlpha" : "soft") }
        else { flag = clipRect ? (alphaClip ? "clipAlpha" : "clip") : "alpha" }
        guard let key = clipMaterialKeys[base]?[flag], materials[key] != nil else { return nil }
        return key
    }

    /// Scene curves are evaluated in their serialized material value space.
    /// Convert a completed source Color/Gamma property once before using it as
    /// a GPU override; ordinary floats, vectors, matrices and alpha stay raw.
    func gpuMaterialValue(_ value: [Float], property: String, materialKey: String) -> [Float] {
        guard let kind = materialPropertyTypes[materialKey]?[property] else { return value }
        return Self.linearMaterialValue(value, type: kind.type, flags: kind.flags)
    }

    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}

    /// Read pixels produced by Metal itself, represented over opaque black.
    /// Encoded linear-premultiplied sRGB RGB cannot be declared as CGImage's
    /// encoded-space premultiplied RGB. The matte retains every raw RGB byte,
    /// including additive color, while diagnostics preserve the original alpha.
    func copyDrawableImage() throws -> CGImage {
        guard renderedFrameGeneration == submittedFrameGeneration else {
            throw Failure.message("Current source frame has no newly rendered Metal drawable")
        }
        guard let device, let texture = lastDrawable?.texture,
              let command = queue.makeCommandBuffer(), let blit = command.makeBlitCommandEncoder() else {
            throw Failure.message("No rendered Metal drawable to capture")
        }
        lastRenderCommand?.waitUntilCompleted()
        if let error = lastRenderCommand?.error { throw error }
        let rowBytes = ((texture.width * 4 + 255) / 256) * 256
        guard let buffer = device.makeBuffer(length: rowBytes * texture.height, options: .storageModeShared) else {
            throw Failure.message("Cannot allocate Metal readback buffer")
        }
        blit.copy(from: texture, sourceSlice: 0, sourceLevel: 0,
                  sourceOrigin: MTLOrigin(x: 0, y: 0, z: 0), sourceSize: MTLSize(width: texture.width, height: texture.height, depth: 1),
                  to: buffer, destinationOffset: 0, destinationBytesPerRow: rowBytes, destinationBytesPerImage: rowBytes * texture.height)
        blit.endEncoding()
        command.commit()
        command.waitUntilCompleted()
        if let error = command.error { throw error }
        let rawData = Data(bytes: buffer.contents(), count: rowBytes * texture.height)
        drawableReadbackReport = try HUDSourceDrawableReadback.analyze(rawBGRA: rawData,
            width: texture.width, height: texture.height, rowBytes: rowBytes)
        drawableReadbackBGRA = rawData
        let data = try HUDSourceDrawableReadback.blackMatteEncodedBGRA(rawBGRA: rawData,
            width: texture.width, height: texture.height, rowBytes: rowBytes)
        guard let provider = CGDataProvider(data: data as CFData),
              let image = CGImage(width: texture.width, height: texture.height, bitsPerComponent: 8, bitsPerPixel: 32,
                                  bytesPerRow: rowBytes, space: CGColorSpace(name: CGColorSpace.sRGB)!,
                                  bitmapInfo: CGBitmapInfo(rawValue: CGBitmapInfo.byteOrder32Little.rawValue | CGImageAlphaInfo.premultipliedFirst.rawValue),
                                  provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent) else {
            throw Failure.message("Cannot construct Metal readback image")
        }
        return image
    }

    /// Original packed RGB scene bytes before the final source composite.
    /// No alpha expansion, output transfer, tone mapping or PNG conversion.
    func copySceneColorReadback() throws -> SceneColorReadback {
        guard sceneColorMode == .sourceRGBHDR,
              renderedFrameGeneration == submittedFrameGeneration,
              let texture = sceneColorTexture, let device else {
            throw Failure.message("Current frame has no original HDR scene target")
        }
        lastRenderCommand?.waitUntilCompleted()
        if let error = lastRenderCommand?.error { throw error }
        let rowBytes = ((texture.width * 4 + 255) / 256) * 256
        guard let buffer = device.makeBuffer(length: rowBytes * texture.height, options: .storageModeShared),
              let command = queue.makeCommandBuffer(), let encoder = command.makeBlitCommandEncoder() else {
            throw Failure.message("Cannot allocate original HDR readback")
        }
        encoder.copy(from: texture, sourceSlice: 0, sourceLevel: 0, sourceOrigin: MTLOrigin(x: 0, y: 0, z: 0),
                     sourceSize: MTLSize(width: texture.width, height: texture.height, depth: 1),
                     to: buffer, destinationOffset: 0, destinationBytesPerRow: rowBytes,
                     destinationBytesPerImage: rowBytes * texture.height)
        encoder.endEncoding(); command.commit(); command.waitUntilCompleted()
        if let error = command.error { throw error }
        return SceneColorReadback(data: Data(bytes: buffer.contents(), count: rowBytes * texture.height),
            width: texture.width, height: texture.height, rowBytes: rowBytes, pixelFormat: "rg11b10Float")
    }

    private func sceneDescriptor(display: MTLRenderPassDescriptor, drawable: CAMetalDrawable,
                                 command: MTLCommandBuffer) throws -> MTLRenderPassDescriptor {
        guard sceneColorMode == .sourceRGBHDR else { return display }
        guard let device else { throw Failure.message("Metal device unavailable") }
        if sceneColorTexture?.width != drawable.texture.width || sceneColorTexture?.height != drawable.texture.height {
            let descriptor = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .rg11b10Float,
                width: drawable.texture.width, height: drawable.texture.height, mipmapped: false)
            descriptor.storageMode = .private
            descriptor.usage = [.renderTarget, .shaderRead]
            guard let texture = device.makeTexture(descriptor: descriptor) else {
                throw Failure.message("Original B10G11R11 RGB HDR target unavailable")
            }
            texture.label = "Source UI3D RGB HDR"
            sceneColorTexture = texture
        }
        // The desktop adapter owns its initial destination contents. Clear it
        // before the source composite's explicit Load, avoiding undefined data.
        // This is not a claim about the game's earlier scene render contents.
        let initialize = MTLRenderPassDescriptor()
        initialize.colorAttachments[0].texture = drawable.texture
        initialize.colorAttachments[0].loadAction = .clear
        initialize.colorAttachments[0].storeAction = .store
        initialize.colorAttachments[0].clearColor = clearColor
        guard let initialization = command.makeRenderCommandEncoder(descriptor: initialize) else {
            throw Failure.message("Cannot initialize final UI attachment")
        }
        initialization.endEncoding()
        let descriptor = display.copy() as! MTLRenderPassDescriptor
        descriptor.colorAttachments[0].texture = sceneColorTexture
        descriptor.colorAttachments[0].loadAction = .clear
        descriptor.colorAttachments[0].storeAction = .store
        descriptor.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0)
        return descriptor
    }

    private func mergeEligible(_ batch: Batch) -> Bool {
        guard batch.mesh.hasPrefix("ui/"), batch.indexRange == nil,
              let geometry = geometries[batch.mesh], !geometry.hasTextChannels,
              geometry.vertices.storageMode == .shared, geometry.indices.storageMode == .shared,
              geometry.vertices.length == geometry.originalVertices.count * MemoryLayout<Vertex>.stride,
              geometry.indices.length >= geometry.indexCount * MemoryLayout<UInt32>.stride,
              geometry.indexCount > 0, geometry.indexCount % 3 == 0,
              let material = materials[batch.material], material.passes.count == 1 else { return false }
        let pass = material.passes[0]
        guard ["image", "imageClipRect"].contains(pass.shaderKey), !pass.depthWrite else { return false }
        let id = ObjectIdentifier(pass)
        if let allowed = mergeSafePasses[id] { return allowed }
        // These source programs use only explicit vertex attributes. A
        // replacement source using draw/primitive identity cannot concatenate.
        let builtins = ["vertex_id", "instance_id", "primitive_id", "base_vertex", "base_instance"]
        let allowed = pass.shader.stages.values.allSatisfy { stage in
            guard let data = try? HUDSourceResourceData.read(root.appendingPathComponent(stage.file)),
                  let code = String(data: data, encoding: .utf8) else { return false }
            return !builtins.contains(where: code.contains)
        }
        mergeSafePasses[id] = allowed
        return allowed
    }

    private func mergeCompatible(_ a: Batch, _ b: Batch) -> Bool {
        a.material == b.material && a.world == b.world
            && a.uniformOverrides == b.uniformOverrides && a.textureOverrides == b.textureOverrides
            && a.stencilOverrides == b.stencilOverrides && a.colorWriteMask == b.colorWriteMask
            && a.appliesDesktopAccent == b.appliesDesktopAccent
    }

    private func vertexColor(for batch: Batch) -> SIMD4<Float> {
        HUDSourceDesktopAccent.replacingYellow(batch.color, accent: batch.appliesDesktopAccent ? desktopAccentLinear : nil)
    }

    private func adjacentBatches(device: MTLDevice) throws -> (batches: [Batch], geometry: [String: Geometry]) {
        var result: [Batch] = [], merged: [String: Geometry] = [:]
        result.reserveCapacity(batches.count)
        var index = 0, slot = 0
        let reusable = geometryBuffersAreIdle
        while index < batches.count {
            let first = batches[index]
            guard mergeEligible(first) else { result.append(first); index += 1; continue }
            var end = index + 1
            while end < batches.count, mergeCompatible(first, batches[end]), mergeEligible(batches[end]) { end += 1 }
            guard end > index + 1 else { result.append(first); index += 1; continue }
            let members = batches[index..<end].map { batch in
                MergedMember(mesh: batch.mesh, revision: geometries[batch.mesh]!.revision,
                    color: vertexColor(for: batch))
            }
            let previous = slot < mergedGeometrySlots.count ? mergedGeometrySlots[slot] : nil
            let geometry: Geometry
            if let previous, previous.members == members {
                geometry = previous.geometry
                mergedGeometryReuseCount += 1
            } else {
                var vertices: [Vertex] = [], indices: [UInt32] = []
                for member in members {
                    let source = geometries[member.mesh]!
                    guard vertices.count <= Int(UInt32.max) - source.originalVertices.count else {
                        throw Failure.message("Source merged geometry exceeds UInt32 index range")
                    }
                    let base = UInt32(vertices.count)
                    let start = vertices.count
                    vertices.append(contentsOf: source.originalVertices)
                    if member.color != SIMD4<Float>(repeating: 1) {
                        // Identical operation/order to the ordinary tint path.
                        for i in start..<vertices.count { vertices[i].color *= member.color }
                    }
                    let words = UnsafeBufferPointer(start: source.indices.contents().assumingMemoryBound(to: UInt32.self),
                        count: source.indexCount)
                    for word in words { indices.append(word + base) }
                }
                let vertexBuffer = try geometryBuffer(vertices, previous: previous?.geometry.vertices, reusable: reusable, device: device)
                let indexBuffer = try geometryBuffer(indices, previous: previous?.geometry.indices, reusable: reusable, device: device)
                geometry = Geometry(vertices: vertexBuffer, indices: indexBuffer, indexCount: indices.count,
                    originalVertices: vertices, revision: (previous?.geometry.revision ?? 0) &+ 1)
                #if HUD_SOURCE_RENDER_PREVIEW
                if verifyMergedGeometryBytesForVerification {
                    let vertexData = vertices.withUnsafeBufferPointer { Data(UnsafeRawBufferPointer($0)) }
                    let indexData = indices.withUnsafeBufferPointer { Data(UnsafeRawBufferPointer($0)) }
                    guard vertexData == Data(bytes: vertexBuffer.contents(), count: vertexData.count),
                          indexData == Data(bytes: indexBuffer.contents(), count: indexData.count) else {
                        throw Failure.message("Merged source vertex/index upload changed bytes")
                    }
                    // Independently recover each original triangle's uploaded
                    // vertex bytes through the remapped indices, in order.
                    var indexOffset = 0, vertexOffset = 0
                    for member in members {
                        let source = geometries[member.mesh]!
                        let sourceIndices = UnsafeBufferPointer(start: source.indices.contents().assumingMemoryBound(to: UInt32.self), count: source.indexCount)
                        for j in 0..<source.indexCount {
                            guard indices[indexOffset + j] == sourceIndices[j] + UInt32(vertexOffset) else {
                                throw Failure.message("Merged source primitive order changed")
                            }
                        }
                        var expected = source.originalVertices
                        if member.color != SIMD4<Float>(repeating: 1) {
                            for i in expected.indices { expected[i].color *= member.color }
                        }
                        let sourceBytes = expected.withUnsafeBufferPointer { Data(UnsafeRawBufferPointer($0)) }
                        let offset = vertexOffset * MemoryLayout<Vertex>.stride
                        guard sourceBytes == vertexData[offset..<(offset + sourceBytes.count)] else {
                            throw Failure.message("Merged source vertex contents changed")
                        }
                        verifiedMergedGeometryByteCount += sourceBytes.count + source.indexCount * 4
                        indexOffset += source.indexCount; vertexOffset += source.originalVertices.count
                    }
                }
                #endif
                let cached = MergedGeometry(members: members, geometry: geometry)
                if slot == mergedGeometrySlots.count { mergedGeometrySlots.append(cached) }
                else { mergedGeometrySlots[slot] = cached }
            }
            let name = "__adjacent_source_group/" + String(slot)
            var batch = first
            batch.mesh = name; batch.color = SIMD4<Float>(repeating: 1)
            merged[name] = geometry; result.append(batch)
            mergedBatchCount += end - index - 1
            slot += 1; index = end
        }
        if mergedGeometrySlots.count > slot { mergedGeometrySlots.removeLast(mergedGeometrySlots.count - slot) }
        return (result, merged)
    }

    func draw(in view: MTKView) {
        // A caller changing the clear value or introducing depth-failure
        // stencil effects leaves the proven desktop subset. Rebuild with the
        // original attachment before obtaining this frame's render pass.
        if depthStencilPixelFormat == .stencil8,
           clearDepth != 1 || batches.contains(where: { ($0.stencilOverrides?.depthFail ?? 0) != 0 }) {
            do {
                guard let device else { return }
                requiresCombinedDepth = true
                materials.removeAll(); materialPropertyTypes.removeAll(); clipMaterialKeys.removeAll()
                materialSets.removeAll(); colorPipelines.removeAll(); stencilStates.removeAll()
                constantBufferABIRecords.removeAll(); uniformCache.removeAll(keepingCapacity: true)
                clearPreparedUniforms()
                try loadMaterials(device: device)
            } catch { diagnostics = [String(describing: error)]; return }
        }
        guard let camera, let device, let display = currentRenderPassDescriptor,
              let drawable = currentDrawable, let command = queue.makeCommandBuffer() else { return }
        diagnostics.removeAll(keepingCapacity: true)
        updateUniformCamera(camera)
        let drawBatches: [Batch]
        let mergedGeometry: [String: Geometry]
        if adjacentBatchMergingEnabled && runtimeSelection != nil {
            do {
                let result = try adjacentBatches(device: device)
                drawBatches = result.batches; mergedGeometry = result.geometry
            } catch { diagnostics.append(String(describing: error)); return }
        } else { drawBatches = batches; mergedGeometry = [:] }
        let descriptor: MTLRenderPassDescriptor
        do { descriptor = try sceneDescriptor(display: display, drawable: drawable, command: command) }
        catch { diagnostics.append(String(describing: error)); return }
        guard let encoder = command.makeRenderCommandEncoder(descriptor: descriptor) else {
            diagnostics.append("Cannot encode source UI scene"); return
        }
        // Both paths use standard clipping; depth bias and custom viewports
        // are deliberately absent from this source renderer.
        encoder.setDepthClipMode(.clip)
        var encodedPassCount = 0
        var bindings = EncoderBindings()
        var resolvedTextures: [ResolvedTexture] = []
        resolvedTextures.reserveCapacity(8)
        let reusableGeometryBuffers = geometryBuffersAreIdle
        var encodedVertexBuffers = Set<ObjectIdentifier>()
        let usePreparedUniforms = runtimeSelection != nil && preparedUniformsEnabled
        if usePreparedUniforms {
            // Keep only slots for the current submitted frame, never history.
            if preparedUniformBatches.count > drawBatches.count {
                preparedUniformBatches.removeLast(preparedUniformBatches.count - drawBatches.count)
            } else if preparedUniformBatches.count < drawBatches.count {
                preparedUniformBatches.append(contentsOf: repeatElement(nil, count: drawBatches.count - preparedUniformBatches.count))
            }
        }
        for (batchIndex, batch) in drawBatches.enumerated() {
            guard let geometry = mergedGeometry[batch.mesh] ?? geometries[batch.mesh], let material = materials[batch.material] else {
                diagnostics.append("Unsupported source mesh/material: \(batch.mesh) / \(batch.material)")
                continue
            }
            if material.needsText,
               (camera.projection == nil || !geometry.hasTextChannels) {
                diagnostics.append("Source TMP requires projection, normals and packed UV2: \(batch.mesh)")
                continue
            }
            let indexRange = batch.indexRange ?? 0..<geometry.indexCount
            guard indexRange.lowerBound >= 0, indexRange.upperBound <= geometry.indexCount,
                  !indexRange.isEmpty, indexRange.lowerBound % 3 == 0, indexRange.count % 3 == 0 else {
                diagnostics.append("Invalid source submesh range: \(batch.mesh)")
                continue
            }
            if material.needsMap,
               batch.uniformOverrides["_WatchWorldToLocalMatrix"] == nil {
                diagnostics.append("Source map requires _WatchWorldToLocalMatrix: \(batch.mesh)")
                continue
            }
            if material.needsSoftMask {
                if let mask = batch.textureOverrides["_SoftMaskTex"] {
                    do { try ensureTexture(named: mask, device: device) }
                    catch { diagnostics.append(String(describing: error)); continue }
                }
                guard batch.uniformOverrides["_WorldToSoftMask"]?.count == 16,
                      batch.uniformOverrides["_SoftMaskTex_ST"]?.count == 4,
                      let maskTexture = batch.textureOverrides["_SoftMaskTex"], textureAssets[maskTexture] != nil else {
                    diagnostics.append("Source soft mask requires its Canvas-local matrix, sprite ST and original texture: \(batch.mesh)")
                    continue
                }
            }
            if material.needsInverseView, camera.inverseView == nil,
               batch.uniformOverrides["unity_MatrixInvV"] == nil || batch.uniformOverrides["_InvViewMatrix"] == nil {
                diagnostics.append("Source shader requires inverse camera view: \(batch.mesh)")
                continue
            }
            if material.needsUIProjection, camera.uiProjectionParameters == nil,
               batch.uniformOverrides["_UIProjectionParams"] == nil {
                diagnostics.append("Source shader requires HG UI projection parameters: \(batch.mesh)")
                continue
            }
            // Source vertices use their original UVs. Missing Unity mesh color
            // channels have the engine's white default, multiplied by the
            // explicit scene vertex tint, not by the shader material tint.
            let vertexColor = vertexColor(for: batch)
            let vertexBuffer: MTLBuffer
            if vertexColor == SIMD4<Float>(repeating: 1) {
                vertexBuffer = geometry.vertices
            } else if let cached = tintedVertices[batch.mesh], cached.color == vertexColor,
                      cached.geometryRevision == geometry.revision {
                vertexBuffer = cached.buffer
            } else {
                var vertices = geometry.originalVertices
                for i in vertices.indices { vertices[i].color *= vertexColor }
                let previous = tintedVertices[batch.mesh]?.buffer
                let buffer: MTLBuffer
                do {
                    buffer = try geometryBuffer(vertices, previous: previous,
                        reusable: reusableGeometryBuffers && !(previous.map({ encodedVertexBuffers.contains(ObjectIdentifier($0)) }) ?? false),
                        device: device)
                } catch {
                    diagnostics.append(String(describing: error)); continue
                }
                // At most one tint buffer per registered geometry; unchanged
                // frames reuse it rather than regenerate source artwork.
                tintedVertices[batch.mesh] = TintedVertices(color: vertexColor, buffer: buffer, geometryRevision: geometry.revision)
                vertexBuffer = buffer
            }
            bindings.bindGeometry(vertexBuffer, encoder: encoder)
            encodedVertexBuffers.insert(ObjectIdentifier(vertexBuffer))
            let preparedBatch: PreparedBatchUniforms?
            if usePreparedUniforms {
                if let cached = preparedUniformBatches[batchIndex], cached.mesh == batch.mesh,
                   cached.material == batch.material, cached.overrides == batch.uniformOverrides,
                   cached.appliesDesktopAccent == batch.appliesDesktopAccent {
                    preparedBatch = cached
                } else {
                    let next = PreparedBatchUniforms(batch: batch)
                    preparedUniformBatches[batchIndex] = next; preparedBatch = next
                }
            } else { preparedBatch = nil }
            for pass in material.passes {
            resolvedTextures.removeAll(keepingCapacity: true)
            var missing: [String] = []
            do {
                for plan in pass.texturePlans {
                    let name = batch.textureOverrides[plan.name] ?? plan.defaultID
                    if let asset = try ensureTexture(named: name, device: device) {
                        resolvedTextures.append(ResolvedTexture(plan: plan, asset: asset))
                    } else { missing.append(name) }
                }
            } catch {
                diagnostics.append(String(describing: error)); continue
            }
            guard missing.isEmpty else {
                diagnostics.append("Missing source textures: " + missing.joined(separator: ", "))
                continue
            }
            do {
                try prepare(pass: pass, values: material.values,
                    propertyTypes: materialPropertyTypes[batch.material] ?? [:], device: device)
                bindings.bindPipeline(try colorPipeline(pass: pass, mask: batch.colorWriteMask), encoder: encoder)
                bindings.bindDepth(try depthState(pass: pass, override: batch.stencilOverrides), encoder: encoder)
            } catch {
                diagnostics.append(String(describing: error))
                continue
            }
            bindings.bindStencil(batch.stencilOverrides?.reference ?? pass.stencilReference, encoder: encoder)
            bindings.bindCull(pass.cull, encoder: encoder)
            if let preparedBatch {
                let cells = preparedUniformCells(pass: pass, batch: preparedBatch)
                for (i, cell) in cells.enumerated() {
                    let bytes = preparedUniformData(cell: cell, world: batch.world, camera: camera)
                    #if HUD_SOURCE_RENDER_PREVIEW
                    if verifyPreparedUniformBytesForVerification {
                        let expected = uniformData(plan: pass.uniformPlans[i], batch: batch, camera: camera)
                        if bytes != expected { diagnostics.append("Prepared source uniform bytes differ: \(pass.id)/\(i)") }
                        verifiedPreparedUniformByteCount += 1
                    }
                    #endif
                    bindings.bindUniform(bytes, vertex: cell.plan.vertex, index: cell.plan.index, encoder: encoder)
                }
            } else {
                for plan in pass.uniformPlans {
                    let bytes = uniformData(plan: plan, batch: batch, camera: camera)
                    bindings.bindUniform(bytes, vertex: plan.vertex, index: plan.index, encoder: encoder)
                }
            }
            for texture in resolvedTextures { bindings.bindTexture(texture, encoder: encoder) }
            encoder.drawIndexedPrimitives(type: .triangle, indexCount: indexRange.count,
                                          indexType: .uint32, indexBuffer: geometry.indices,
                                          indexBufferOffset: indexRange.lowerBound * MemoryLayout<UInt32>.size)
            encodedPassCount += 1
            }
        }
        encoderBindingChangeCount += bindings.changes
        encoderBindingSkipCount += bindings.skips
        encodedPassDrawCount += encodedPassCount
        sourcePassDrawCount += adjacentBatchMergingEnabled && runtimeSelection != nil
            ? batches.reduce(0) { $0 + (materials[$1.material]?.passes.count ?? 0) } : encodedPassCount
        encoder.endEncoding()
        if sceneColorMode == .sourceRGBHDR {
            guard let uiComposite, let sceneColorTexture else {
                diagnostics.append("Original UI HDR composite unavailable"); return
            }
            do {
                // This source fullscreen stage pairs UV.y=1-quadY with a
                // negated clip Y. The Metal top-left RT adapter requires flipY
                // 1 to preserve row order; independent asymmetric GPU fixtures
                // verify it rather than reusing the UI3D camera's flip tuple.
                try uiComposite.encode(command: command, input: sceneColorTexture,
                    destination: drawable.texture, flipX: 0, flipY: 1)
            } catch { diagnostics.append(String(describing: error)); return }
        }
        lastDrawable = drawable
        lastRenderCommand = command
        renderedFrameGeneration = submittedFrameGeneration
        if !measuringFirstDrawable, encodedPassCount > 0 {
            measuringFirstDrawable = true
            // Measure a drawable containing real source draws, not the empty
            // entrance clear. Callback timestamps share CACurrentMediaTime's
            // host clock, and weak references do not extend overlay lifetime.
            command.addCompletedHandler { [weak self] completed in
                guard completed.status == .completed else { return }
                let timestamp = CACurrentMediaTime()
                DispatchQueue.main.async { [weak self] in
                    self?.firstCompletedFrameTimestampForVerification = timestamp
                }
            }
        }
        if #available(macOS 10.15.4, *), encodedPassCount > 0,
           firstPresentedFrameTimestampForVerification == nil, !awaitingFirstPresentation,
           window?.isVisible == true, !isHiddenOrHasHiddenAncestor {
            awaitingFirstPresentation = true
            drawable.addPresentedHandler { [weak self] presented in
                let timestamp = presented.presentedTime
                DispatchQueue.main.async { [weak self] in
                    guard let self else { return }
                    self.awaitingFirstPresentation = false
                    if timestamp > 0, self.firstPresentedFrameTimestampForVerification == nil {
                        self.firstPresentedFrameTimestampForVerification = timestamp
                    }
                }
            }
        }
        command.present(drawable)
        command.commit()
    }

    private func updateUniformCamera(_ camera: Camera) {
        func equal(_ a: simd_float4x4?, _ b: simd_float4x4?) -> Bool {
            switch (a, b) { case (nil, nil): return true; case let (a?, b?): return a == b; default: return false }
        }
        if let previous = uniformCamera {
            if previous.viewProjection != camera.viewProjection || previous.viewNoTranslationProjection != camera.viewNoTranslationProjection
                || previous.worldSpacePosition != camera.worldSpacePosition || previous.renderPathInjected != camera.renderPathInjected
                || previous.flipX != camera.flipX || previous.flipY != camera.flipY
                || !equal(previous.projection, camera.projection) || !equal(previous.inverseView, camera.inverseView)
                || previous.uiProjectionParameters != camera.uiProjectionParameters || uniformDrawableSize != drawableSize {
                uniformCameraVersion &+= 1
            }
            if previous.timeSeconds != camera.timeSeconds { uniformTimeVersion &+= 1 }
        } else { uniformCameraVersion &+= 1; uniformTimeVersion &+= 1 }
        uniformCamera = camera
        uniformDrawableSize = drawableSize
    }

    private func preparedUniformCells(pass: Pass, batch: PreparedBatchUniforms) -> [PreparedUniformCell] {
        let id = ObjectIdentifier(pass)
        if let cells = batch.passes[id] { return cells }
        let cells = pass.uniformPlans.map { original -> PreparedUniformCell in
            let (resolved, hasOverrides) = resolvedUniformPlan(original, overrides: batch.overrides)
            let sharedKey = SharedUniformKey(plan: resolved.key, appliesDesktopAccent: batch.appliesDesktopAccent)
            // These are the same interfaces/material values already shared
            // by the authoritative cache. Overrides and world transforms
            // stay private so two instances of one mesh cannot alias inputs.
            if !hasOverrides && !resolved.needsWorld,
               let shared = sharedPreparedUniforms[sharedKey] { return shared }
            let cell = PreparedUniformCell(plan: resolved, appliesDesktopAccent: batch.appliesDesktopAccent)
            if !hasOverrides && !resolved.needsWorld { sharedPreparedUniforms[sharedKey] = cell }
            preparedUniformResolutionCount += 1
            return cell
        }
        batch.passes[id] = cells
        return cells
    }

    private func resolvedUniformPlan(_ original: UniformPlan, overrides: [String: [Float]]) -> (UniformPlan, Bool) {
        var resolved = original
        var hasOverrides = false
        for i in resolved.fields.indices {
            if let value = overrides[resolved.fields[i].name] {
                hasOverrides = true
                resolved.fields[i].value = value
                resolved.fields[i].dynamic = .none
            }
        }
        resolved.needsWorld = resolved.fields.contains { $0.dynamic == .world }
        resolved.needsTime = resolved.fields.contains { $0.dynamic == .time || $0.dynamic == .uiTime }
        resolved.needsCamera = resolved.fields.contains {
            $0.dynamic != .none && $0.dynamic != .world && $0.dynamic != .time && $0.dynamic != .uiTime
        }
        return (resolved, hasOverrides)
    }

    #if HUD_SOURCE_RENDER_PREVIEW
    func verifyPreparedUniformFieldOrderForVerification() throws {
        guard var camera else { throw Failure.message("Uniform verification requires a submitted camera") }
        let fields = [
            UniformFieldPlan(name: "A", offset: 0, value: [1, 2, 3, 4], isColor: false, dynamic: .none),
            UniformFieldPlan(name: "T", offset: 4, value: nil, isColor: false, dynamic: .time),
            UniformFieldPlan(name: "W", offset: 16, value: nil, isColor: false, dynamic: .world),
            UniformFieldPlan(name: "C", offset: 20, value: [0.9, 0.8, 0.1, 1], isColor: true, dynamic: .none)
        ]
        let plan = UniformPlan(key: "verification/overlap", vertex: true, index: 0, byteCount: 96,
            fields: fields, fieldNames: Set(fields.map(\.name)), needsWorld: true, needsCamera: false, needsTime: true)
        let cases: [[String: [Float]]] = [[:], ["A": []], ["T": [0.25]], ["W": [1, 2, 3]],
            ["C": [0.7]], ["A": [], "T": [], "W": [], "C": []], ["unrelated": [42]]]
        for overrides in cases {
            let resolved = resolvedUniformPlan(plan, overrides: overrides).0
            let cell = PreparedUniformCell(plan: resolved)
            for step in 0..<3 {
                camera.timeSeconds = Float(step) / 3
                var world = matrix_identity_float4x4; world.columns.3.x = Float(step)
                updateUniformCamera(camera)
                let expected = encodeUniformFields(plan: plan, overrides: overrides, world: world, camera: camera)
                let actual = preparedUniformData(cell: cell, world: world, camera: camera)
                let repeated = preparedUniformData(cell: cell, world: world, camera: camera)
                guard expected == actual, actual == repeated else {
                    throw Failure.message("Prepared uniform field-order/partial-override verification failed")
                }
                verifiedPreparedUniformByteCount += 2
            }
        }
    }
    #endif

    private func preparedUniformData(cell: PreparedUniformCell, world: simd_float4x4, camera: Camera) -> Data {
        let plan = cell.plan
        let cameraVersion = plan.needsCamera ? uniformCameraVersion : 0
        let timeVersion = plan.needsTime ? uniformTimeVersion : 0
        if let bytes = cell.data, cell.cameraVersion == cameraVersion, cell.timeVersion == timeVersion,
           !plan.needsWorld || cell.world.map({ $0 == world }) == true {
            preparedUniformHitCount += 1
            return bytes
        }
        let bytes = encodeUniformFields(plan: plan, overrides: nil, world: world, camera: camera,
            appliesDesktopAccent: cell.appliesDesktopAccent)
        cell.data = bytes
        cell.cameraVersion = cameraVersion; cell.timeVersion = timeVersion
        cell.world = plan.needsWorld ? world : nil
        return bytes
    }

    private func uniformData(plan: UniformPlan, batch: Batch, camera: Camera) -> Data {
        let hasOverrides = batch.uniformOverrides.keys.contains { plan.fieldNames.contains($0) }
        let overrides = hasOverrides ? batch.uniformOverrides : [:]
        let key = UniformCacheKey(plan: plan.key, mesh: hasOverrides || plan.needsWorld ? batch.mesh : "",
            appliesDesktopAccent: batch.appliesDesktopAccent)
        let cameraVersion = plan.needsCamera ? uniformCameraVersion : 0
        let timeVersion = plan.needsTime ? uniformTimeVersion : 0
        if let cached = uniformCache[key], cached.overrides == overrides,
           cached.cameraVersion == cameraVersion, cached.timeVersion == timeVersion,
           !plan.needsWorld || cached.world.map({ $0 == batch.world }) == true {
            uniformCacheHitCount += 1
            return cached.data
        }
        // Rebuild from zero on a changed input, preserving the original write
        // order and even partial property overrides exactly. Unchanged camera
        // and material buffers reuse their completed immutable bytes.
        let bytes = encodeUniformFields(plan: plan, overrides: overrides, world: batch.world, camera: camera,
            appliesDesktopAccent: batch.appliesDesktopAccent)
        // One current value per dependency set, never a frame-history cache.
        // Extra custom shortcuts remain correct without growing this bound.
        if uniformCache[key] != nil || uniformCache.count < 1024 {
            uniformCache[key] = UniformCacheEntry(overrides: overrides, world: plan.needsWorld ? batch.world : nil,
                cameraVersion: cameraVersion, timeVersion: timeVersion, data: bytes)
        }
        return bytes
    }

    private func encodeUniformFields(plan: UniformPlan, overrides: [String: [Float]]?,
                                     world: simd_float4x4, camera: Camera, appliesDesktopAccent: Bool = true) -> Data {
        // Start from zero and retain field order, including partial overrides
        // and overlapping fields. Prepared cells only resolve value lookup;
        // they never patch a previous buffer in a different write order.
        var bytes = Data(repeating: 0, count: plan.byteCount)
        for field in plan.fields {
            if let value = overrides?[field.name] ?? field.value {
                Self.put(HUDSourceDesktopAccent.materialValue(value, isColor: field.isColor,
                    accent: appliesDesktopAccent ? desktopAccentLinear : nil), into: &bytes, at: field.offset)
                continue
            }
            switch field.dynamic {
            case .world: Self.put(world, into: &bytes, at: field.offset)
            case .viewProjection: Self.put(camera.viewProjection, into: &bytes, at: field.offset)
            case .viewNoTranslation: Self.put(camera.viewNoTranslationProjection, into: &bytes, at: field.offset)
            case .projection:
                if let value = camera.projection { Self.put(value, into: &bytes, at: field.offset) }
            case .inverseView:
                if let value = camera.inverseView { Self.put(value, into: &bytes, at: field.offset) }
            case .uiProjection:
                if let value = camera.uiProjectionParameters { Self.put([value.x, value.y, value.z, value.w], into: &bytes, at: field.offset) }
            case .cameraPosition: Self.put([camera.worldSpacePosition.x, camera.worldSpacePosition.y, camera.worldSpacePosition.z, 0], into: &bytes, at: field.offset)
            case .uiTime: Self.put([camera.timeSeconds * 0.05, camera.timeSeconds, camera.timeSeconds * 2, 0], into: &bytes, at: field.offset)
            case .time: Self.put([camera.timeSeconds / 20, camera.timeSeconds, camera.timeSeconds * 2, camera.timeSeconds * 3], into: &bytes, at: field.offset)
            case .screen:
                let width = Float(drawableSize.width), height = Float(drawableSize.height)
                if width > 0, height > 0 { Self.put([width, height, 1 / width, 1 / height], into: &bytes, at: field.offset) }
            case .renderPath: Self.put([camera.renderPathInjected], into: &bytes, at: field.offset)
            case .flipX: Self.put([camera.flipX], into: &bytes, at: field.offset)
            case .flipY: Self.put([camera.flipY], into: &bytes, at: field.offset)
            case .none: break
            }
        }
        uniformEncodeCount += 1
        return bytes
    }

    private static func put(_ values: [Float], into data: inout Data, at offset: Int) {
        guard offset >= 0, offset + values.count * MemoryLayout<Float>.size <= data.count else { return }
        values.withUnsafeBufferPointer { values in
            let bytes = UnsafeRawBufferPointer(values)
            data.replaceSubrange(offset..<(offset + bytes.count), with: bytes)
        }
    }

    private func colorPipeline(pass: Pass, mask: UInt8?) throws -> MTLRenderPipelineState {
        guard let pipeline = pass.pipeline else { throw Failure.message("Unprepared source pipeline") }
        guard let mask else { return pipeline }
        let key = pass.id + "/color/" + String(mask)
        if let cached = colorPipelines[key] { return cached }
        guard let device else { throw Failure.message("Metal device unavailable") }
        let attachment = pass.pipelineDescriptor.colorAttachments[0]!
        let previous = attachment.writeMask
        attachment.writeMask = Self.colorMask(Int(mask))
        defer { attachment.writeMask = previous }
        let result = try device.makeRenderPipelineState(descriptor: pass.pipelineDescriptor)
        // Source mask configurations are finite; do not retain unbounded
        // externally supplied state combinations.
        if colorPipelines.count >= 128 { colorPipelines.removeAll(keepingCapacity: true) }
        colorPipelines[key] = result
        return result
    }

    private func depthState(pass: Pass, override: StencilState?) throws -> MTLDepthStencilState {
        guard let override else { return pass.depth }
        let key = StencilKey(pass: pass.id, state: override)
        if let cached = stencilStates[key] { return cached }
        let depth = MTLDepthStencilDescriptor()
        depth.depthCompareFunction = pass.depthCompare
        depth.isDepthWriteEnabled = pass.depthWrite
        let stencil = MTLStencilDescriptor()
        stencil.stencilCompareFunction = try Self.compare(Float(override.compare))
        stencil.stencilFailureOperation = try Self.stencil(Float(override.fail))
        stencil.depthFailureOperation = try Self.stencil(Float(override.depthFail))
        stencil.depthStencilPassOperation = try Self.stencil(Float(override.pass))
        stencil.readMask = override.readMask
        stencil.writeMask = override.writeMask
        depth.frontFaceStencil = stencil
        depth.backFaceStencil = stencil
        guard let result = device?.makeDepthStencilState(descriptor: depth) else { throw Failure.message("Cannot create source mask stencil state") }
        if stencilStates.count >= 256 { stencilStates.removeAll(keepingCapacity: true) }
        stencilStates[key] = result
        return result
    }

    private static func colorMask(_ bits: Int) -> MTLColorWriteMask {
        var mask: MTLColorWriteMask = []
        if bits & 1 != 0 { mask.insert(.red) }
        if bits & 2 != 0 { mask.insert(.green) }
        if bits & 4 != 0 { mask.insert(.blue) }
        if bits & 8 != 0 { mask.insert(.alpha) }
        return mask
    }

    private static func put(_ matrix: simd_float4x4, into data: inout Data, at offset: Int) {
        // The generated MSL consumes native column vectors (including its
        // explicit VP transpose access). Write logical Swift matrix columns;
        // never reinterpret this as the original Vulkan host ABI.
        var matrix = matrix
        withUnsafeBytes(of: &matrix) { bytes in
            guard offset >= 0, offset + bytes.count <= data.count else { return }
            data.replaceSubrange(offset..<(offset + bytes.count), with: bytes)
        }
    }

    private func object(_ name: String) throws -> Any {
        if let cached = metadata?.objects[name] { return cached }
        return try JSONSerialization.jsonObject(with: HUDSourceResourceData.read(root.appendingPathComponent(name)))
    }

    private func loadGeometries(device: MTLDevice) throws {
        for name in ["Equipring", "watchline", "Plane", "Cylinder"] {
            guard let mesh = try object("Meshes/\(name).json") as? [String: Any],
                  let positions = mesh["positions"] as? [[NSNumber]],
                  let uv = mesh["uv0"] as? [[NSNumber]], let indices = mesh["indices"] as? [NSNumber],
                  positions.count == uv.count else { throw Failure.message("Invalid source geometry: \(name)") }
            let colors = mesh["colors"] as? [[NSNumber]] ?? []
            var vertices = [Vertex]()
            for i in positions.indices {
                guard positions[i].count >= 3, uv[i].count >= 2 else { throw Failure.message("Truncated source vertex: \(name)") }
                let p = positions[i], t = uv[i]
                var color = SIMD4<Float>(repeating: 1)
                if colors.count == positions.count, colors[i].count >= 4 {
                    color = SIMD4(colors[i][0].floatValue, colors[i][1].floatValue, colors[i][2].floatValue, colors[i][3].floatValue)
                }
                vertices.append(Vertex(position: SIMD4(p[0].floatValue, p[1].floatValue, p[2].floatValue, 1),
                                       uv: SIMD2(t[0].floatValue, t[1].floatValue), color: color))
            }
            let indexWords = indices.map(\.uint32Value)
            guard indexWords.allSatisfy({ Int($0) < vertices.count }), indexWords.count % 3 == 0,
                  let vertexBuffer = vertices.withUnsafeBufferPointer({ device.makeBuffer(bytes: $0.baseAddress!, length: $0.count * MemoryLayout<Vertex>.stride, options: .storageModeShared) }),
                  let indexBuffer = indexWords.withUnsafeBufferPointer({ device.makeBuffer(bytes: $0.baseAddress!, length: $0.count * MemoryLayout<UInt32>.stride, options: .storageModeShared) }) else {
                throw Failure.message("Invalid source index buffer: \(name)")
            }
            geometries[name] = Geometry(vertices: vertexBuffer, indices: indexBuffer, indexCount: indexWords.count, originalVertices: vertices)
        }
    }

    private func loadTextures(device: MTLDevice) throws {
        guard let textures = try object("textures.json") as? [[String: Any]] else { throw Failure.message("Invalid source texture metadata") }
        let selection = try selectedIndices(runtimeSelection?.texture_indices, count: textures.count)
        for index in selection {
            let info = textures[index]
            // Validate the available descriptor now, without touching pixel
            // data or Metal allocation. Reference fixtures retain eager loads.
            guard let id = info["path_id"] as? String, info["data_file"] is String,
                  let width = info["width"] as? Int, width > 0,
                  let height = info["height"] as? Int, height > 0,
                  let format = info["texture_format"] as? Int, [25, 4, 63].contains(format),
                  let mipCount = info["mip_count"] as? Int, mipCount > 0,
                  info["sampler"] is [String: Any],
                  !(format == 63 && (info["color_space"] as? Int) == 0) else {
                throw Failure.message("Incomplete source texture metadata")
            }
            var aliases = [id]
            if let cab = info["cab"] as? String { aliases.append(cab + ":" + id) }
            let pending = PendingTexture(id: id, aliases: aliases, info: info)
            for alias in aliases { pendingTextures[alias] = pending }
            if runtimeSelection == nil { try ensureTexture(named: id, device: device) }
        }
        let descriptor = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .rgba8Unorm, width: 1, height: 1, mipmapped: false)
        guard let white = device.makeTexture(descriptor: descriptor), let sampler = device.makeSamplerState(descriptor: MTLSamplerDescriptor()) else {
            throw Failure.message("Cannot create shader's default white texture")
        }
        let pixel: [UInt8] = [255, 255, 255, 255]
        pixel.withUnsafeBufferPointer { white.replace(region: MTLRegionMake2D(0, 0, 1, 1), mipmapLevel: 0, withBytes: $0.baseAddress!, bytesPerRow: 4) }
        textureAssets["__white"] = TextureAsset(texture: white, sampler: sampler)
    }

    @discardableResult
    private func ensureTexture(named name: String, device: MTLDevice) throws -> TextureAsset? {
        if let asset = textureAssets[name] { return asset }
        guard let pending = pendingTextures[name] else { return nil }
        if let failure = textureFailures[pending.id] { throw Failure.message(failure) }
        do {
            let asset = try loadTexture(info: pending.info, device: device)
            sourceTextureLoadCount += 1
            for alias in pending.aliases {
                // An explicitly registered dynamic alias takes precedence.
                if textureAssets[alias] == nil { textureAssets[alias] = asset }
                pendingTextures.removeValue(forKey: alias)
            }
            return asset
        } catch {
            let failure = "Cannot load source texture " + pending.id + ": " + String(describing: error)
            textureFailures[pending.id] = failure
            throw Failure.message(failure)
        }
    }

    private func loadTexture(info: [String: Any], device: MTLDevice) throws -> TextureAsset {
        guard let id = info["path_id"] as? String, let file = info["data_file"] as? String,
              let width = info["width"] as? Int, let height = info["height"] as? Int,
              let format = info["texture_format"] as? Int, let mipCount = info["mip_count"] as? Int,
              let samplerInfo = info["sampler"] as? [String: Any] else { throw Failure.message("Incomplete source texture metadata") }
        let sRGB = (info["color_space"] as? Int) == 0
        // Preserve every authored mip. BC7 fallback decodes each source
        // mip independently; original RGBA32 stays in its source format.
        guard format == 25 || format == 4 || format == 63 else { throw Failure.message("Unmapped original source texture format: \(id)") }
        // The Intel build retains the app's macOS 10.15.4 minimum. Older
        // systems use the already exported pixels of every original mip.
        var supportsOriginalBC7 = false
        if #available(macOS 11.0, *) { supportsOriginalBC7 = device.supportsBCTextureCompression }
        let useBC = format == 25 && supportsOriginalBC7
        let pixelFormat: MTLPixelFormat
        if format == 63 {
            guard !sRGB else { throw Failure.message("Unexpected gamma R8 source atlas: \(id)") }
            pixelFormat = .r8Unorm
        }
        else if format == 4 { pixelFormat = sRGB ? .rgba8Unorm_srgb : .rgba8Unorm }
        else if useBC { pixelFormat = sRGB ? .bc7_rgbaUnorm_srgb : .bc7_rgbaUnorm }
        else { pixelFormat = sRGB ? .bgra8Unorm_srgb : .bgra8Unorm }
        let descriptor = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: pixelFormat,
                                                                  width: width, height: height, mipmapped: mipCount > 1)
        descriptor.mipmapLevelCount = mipCount
        // Leave CPU-visible texture storage at Metal's hardware default:
        // Apple shared, Intel/AMD managed. Forcing shared rejects Intel.
        descriptor.usage = .shaderRead
        guard let texture = device.makeTexture(descriptor: descriptor) else { throw Failure.message("Cannot create source BC7 texture") }
        let selectedFile = format != 25 || useBC ? file : (info["decoded_mips_file"] as? String ?? "")
        guard !selectedFile.isEmpty else { throw Failure.message("Original BC7 decode fallback unavailable: \(id)") }
        let data = try HUDSourceResourceData.read(root.appendingPathComponent(selectedFile))
        var cursor = 0
        for level in 0..<mipCount {
            let w = max(1, width >> level), h = max(1, height >> level)
            let rowBytes = useBC ? ((w + 3) / 4) * 16 : w * (format == 63 ? 1 : 4)
            let count = rowBytes * (useBC ? (h + 3) / 4 : h)
            guard cursor + count <= data.count else { throw Failure.message("Truncated source mip chain: \(id)") }
            data.withUnsafeBytes { raw in
                texture.replace(region: MTLRegionMake2D(0, 0, w, h), mipmapLevel: level,
                                withBytes: raw.baseAddress!.advanced(by: cursor), bytesPerRow: rowBytes)
            }
            cursor += count
        }
        guard cursor == data.count else { throw Failure.message("Unexpected source mip tail: \(id)") }
        let sampler = MTLSamplerDescriptor()
        sampler.minFilter = (samplerInfo["m_FilterMode"] as? Int) == 0 ? .nearest : .linear
        sampler.magFilter = sampler.minFilter
        sampler.mipFilter = (samplerInfo["m_FilterMode"] as? Int) == 2 ? .linear : .nearest
        sampler.sAddressMode = Self.addressMode(samplerInfo["m_WrapU"] as? Int ?? 1)
        sampler.tAddressMode = Self.addressMode(samplerInfo["m_WrapV"] as? Int ?? 1)
        sampler.rAddressMode = Self.addressMode(samplerInfo["m_WrapW"] as? Int ?? 1)
        sampler.maxAnisotropy = max(1, min(16, samplerInfo["m_Aniso"] as? Int ?? 1))
        guard let state = device.makeSamplerState(descriptor: sampler) else { throw Failure.message("Cannot create source sampler") }
        return TextureAsset(texture: texture, sampler: state)
    }

    private func loadFontTextures() throws {
        guard let fonts = try object("Scene/fonts.json") as? [String: Any],
              let source = fonts["source_files"] as? [String: Any],
              let atlases = source["atlas_textures"] as? [[String: Any]] else {
            throw Failure.message("Source font atlas metadata unavailable")
        }
        for atlas in atlases {
            guard let id = atlas["id"] as? String, let format = atlas["format"] as? Int, format == 63,
                  let png = atlas["png"] as? [String: Any], let file = png["file"] as? String,
                  let sampler = atlas["raw_sampler"] as? [String: Any] else {
                throw Failure.message("Unreviewed source SDF atlas")
            }
            if textureAssets[id] != nil { continue }
            // Original atlas is linear R8. The fallback PNG preserves the
            // red channel, which is sampled by the original SDF program.
            try registerTexture(named: id, url: root.appendingPathComponent("Scene/" + file), sRGB: false,
                                filterMode: sampler["m_FilterMode"] as? Int ?? 1,
                                wrapU: sampler["m_WrapU"] as? Int ?? 0, wrapV: sampler["m_WrapV"] as? Int ?? 0)
        }
    }

    private static func materialRecords(object: Any, compact: Bool, selection: RuntimeSelection?) throws -> [[String: Any]] {
        if compact {
            guard let selection, let catalog = object as? [String: Any],
                  catalog["schema"] as? Int == 1, let expectedSource = selection.materials_sha256,
                  catalog["source_sha256"] as? String == expectedSource,
                  let indices = catalog["source_indices"] as? [Int], indices == selection.material_indices,
                  !indices.isEmpty, Set(indices).count == indices.count, indices.allSatisfy({ $0 >= 0 }),
                  let selected = catalog["materials"] as? [[String: Any]], selected.count == indices.count else {
                throw Failure.message("Invalid compact Watch material metadata")
            }
            return selected
        }
        guard let records = object as? [[String: Any]] else { throw Failure.message("Invalid source shader/material metadata") }
        guard let indices = selection?.material_indices else { return records }
        guard !indices.isEmpty, Set(indices).count == indices.count,
              indices.allSatisfy({ $0 >= 0 && $0 < records.count }) else {
            throw Failure.message("Invalid Watch runtime catalog selection")
        }
        return indices.map { records[$0] }
    }

    private static func parseMaterialInputs(record: [String: Any]) throws -> MaterialInputs {
        guard let name = record["name"] as? String, let serialized = record["serialized"] as? [String: Any],
              let properties = serialized["m_SavedProperties"] as? [String: Any] else { throw Failure.message("Incomplete source material") }
        var values: [String: [Float]] = [:]
        for pair in properties["m_Floats"] as? [[Any]] ?? [] {
            if let key = pair.first as? String, let value = pair.last as? NSNumber { values[key] = [value.floatValue] }
        }
        for pair in properties["m_Colors"] as? [[Any]] ?? [] {
            if let key = pair.first as? String, let value = pair.last as? [String: NSNumber] {
                values[key] = ["r", "g", "b", "a"].map { value[$0]?.floatValue ?? 0 }
            }
        }
        guard let shaderProperties = record["shader_properties"] as? [[String: Any]] else {
            throw Failure.message("Source material property color-space metadata unavailable: \(name)")
        }
        var propertyTypes: [String: (type: Int, flags: Int)] = [:]
        for property in shaderProperties {
            guard let key = property["name"] as? String, let type = property["type"] as? Int,
                  let flags = property["flags"] as? Int else { continue }
            propertyTypes[key] = (type, flags)
            if let value = values[key] { values[key] = linearMaterialValue(value, type: type, flags: flags) }
        }
        var textureIDs: [String: String] = [:]
        for binding in record["texture_bindings"] as? [[String: Any]] ?? [] {
            guard let slot = binding["slot"] as? String,
                  let scale = binding["scale"] as? [String: NSNumber], let offset = binding["offset"] as? [String: NSNumber] else { continue }
            values[slot + "_ST"] = [scale["x"]?.floatValue ?? 1, scale["y"]?.floatValue ?? 1, offset["x"]?.floatValue ?? 0, offset["y"]?.floatValue ?? 0]
            if let texture = binding["texture"] as? [String: Any], let id = texture["path_id"] as? String { textureIDs[slot] = id }
        }
        let sourceShader = record["shader"] as? [String: Any]
        let keywords = Set(serialized["m_ValidKeywords"] as? [String] ?? [])
        return MaterialInputs(name: name, id: record["id"] as? String, values: values, propertyTypes: propertyTypes,
            textureIDs: textureIDs, shaderID: sourceShader?["path_id"] as? String, keywords: keywords,
            clipVariants: record["clip_variants"] as? [String: String])
    }

    private static func shaderKey(inputs: MaterialInputs, passName: String) -> String {
        let isFX = inputs.shaderID == "-7864008769510089003"
        let isFont = inputs.shaderID == "2786552470741801451"
        let isMap = inputs.shaderID == "505394952752169778"
        let keywords = inputs.keywords
        if isMap {
            if keywords.contains("_USE_CONTOUR") { return "map13" }
            else if keywords.contains("_USE_BUILDING") { return "map14" }
            else if keywords.contains("_USE_POINTCLOUD") { return "map16" }
            else if keywords.contains("_USE_OUTLINE") { return keywords.contains("_ALPHATEST_ON") ? "map17" : "map15" }
            else { return "map12" }
        }
        else if isFX { return keywords.contains("HG_UI_VFX_DISSOLVE") ? "fx" : keywords.contains("HG_UI_VFX_MASKTEX") ? "fx13" : "fx12" }
        else if isFont {
            let base = (keywords.contains("UNDERLAY_ON") ? "fontUnderlay" : "font") + (keywords.contains("HG_SOFT_MASKABLE") ? "SoftMask" : "")
            let clip = keywords.contains("UNITY_UI_CLIP_RECT"), alpha = keywords.contains("UNITY_UI_ALPHACLIP")
            return base + (clip ? (alpha ? "ClipRectAlpha" : "ClipRect") : (alpha ? "AlphaClip" : ""))
        }
        else {
            let base: String
            if passName == "Default-Stencil-Alpha-Blend" { base = "imageStencil" }
            else if keywords.contains("HG_UI_VFX_DISSOLVE") { base = "imageDissolveFX" }
            else if keywords.contains("HG_UI_VFX_MAINTEX") { base = "imageMainFX" }
            else if keywords.contains("HG_WORLD_UI") { base = "imageWorld" }
            else { base = "image" }
            let clip = keywords.contains("UNITY_UI_CLIP_RECT"), alpha = keywords.contains("UNITY_UI_ALPHACLIP")
            return base + (keywords.contains("HG_SOFT_MASKABLE") ? "SoftMask" : "") + (clip ? (alpha ? "ClipRectAlpha" : "ClipRect") : (alpha ? "AlphaClip" : ""))
        }
    }

    @discardableResult private func loadMaterials(device: MTLDevice, recordStartupTimings: Bool = false) throws -> [String: Double] {
        let started = recordStartupTimings ? CACurrentMediaTime() : 0
        var previous = started, timings: [String: Double] = [:]
        func mark(_ name: String) {
            guard recordStartupTimings else { return }
            let now = CACurrentMediaTime(); timings["materials." + name] = (now - previous) * 1000; previous = now
        }
        let compact = runtimeSelection != nil && FileManager.default.fileExists(atPath: root.appendingPathComponent("runtime-materials.json").path)
        var records = try Self.materialRecords(object: object(compact ? "runtime-materials.json" : "materials.json"),
            compact: compact, selection: runtimeSelection)
        if sceneColorMode == .sourceRGBHDR {
            let url = root.appendingPathComponent("HDR/WatchBlur/material-runtime.json")
            if FileManager.default.fileExists(atPath: url.path) {
                guard let record = try JSONSerialization.jsonObject(with: HUDSourceResourceData.read(url)) as? [String: Any] else {
                    throw Failure.message("Original WatchBlur material unavailable")
                }
                records.append(record)
            }
        }
        mark("records")
        depthStencilPixelFormat = try desktopDepthIsRedundant(records: records) ? .stencil8 : .depth32Float_stencil8
        mark("depthEligibility")
        for (index, record) in records.enumerated() {
            let inputs = index < (metadata?.materialInputs.count ?? 0)
                ? metadata!.materialInputs[index] : try Self.parseMaterialInputs(record: record)
            let name = inputs.name, values = inputs.values, propertyTypes = inputs.propertyTypes, textureIDs = inputs.textureIDs
            let isFX = inputs.shaderID == "-7864008769510089003"
            let isFont = inputs.shaderID == "2786552470741801451"
            let isMap = inputs.shaderID == "505394952752169778"
            var passes: [Pass] = []
            for sourcePass in record["static_pass_states"] as? [[String: Any]] ?? [] {
            if (sourcePass["disabled_in_serialized_material"] as? Bool) == true { continue }
            guard let passName = sourcePass["name"] as? String, let state = sourcePass["state"] as? [String: Any] else { throw Failure.message("Incomplete source pass") }
            if passName != "Default" && passName != "Default-Stencil-Alpha-Blend" && !(isMap && passName == "ForwardOnly") { continue }
            let key = Self.shaderKey(inputs: inputs, passName: passName)
            guard let shader = shaders[key],
                  let blend = state["rtBlend0"] as? [String: Any], let stencilOp = state["stencilOp"] as? [String: Any] else {
                throw Failure.message("Unmapped source pass interface: \(name) / \(passName)")
            }
            func number(_ object: [String: Any], _ key: String) throws -> Float {
                guard let value = object[key] as? [String: Any], let scalar = value["value"] as? NSNumber else {
                    throw Failure.message("Unresolved source pass state: \(name) / \(key)")
                }
                return scalar.floatValue
            }
            let pipeline = MTLRenderPipelineDescriptor()
            let layout = MTLVertexDescriptor()
            let attributes: [(Int, MTLVertexFormat, Int)]
            if isMap {
                attributes = [(0, .float4, 0), (1, .float2, 16), (2, .float2, 64), (3, .float3, 48), (5, .float4, 32)]
            } else if isFont {
                attributes = [(0, .float4, 0), (1, .float3, 48), (2, .float4, 32), (3, .float2, 16), (4, .float2, 64)]
            } else if isFX { attributes = [(0, .float4, 0), (1, .float2, 16), (2, .float4, 32)] }
            else { attributes = [(0, .float4, 0), (1, .float4, 32), (2, .float2, 16)] }
            for (index, format, offset) in attributes {
                layout.attributes[index].format = format
                layout.attributes[index].offset = offset
                layout.attributes[index].bufferIndex = 30
            }
            layout.layouts[30].stride = MemoryLayout<Vertex>.stride
            layout.layouts[30].stepFunction = .perVertex
            pipeline.vertexDescriptor = layout
            pipeline.depthAttachmentPixelFormat = depthStencilPixelFormat == .stencil8 ? .invalid : depthStencilPixelFormat
            pipeline.stencilAttachmentPixelFormat = depthStencilPixelFormat
            let color = pipeline.colorAttachments[0]!
            color.pixelFormat = sceneColorPixelFormat
            color.isBlendingEnabled = true
            color.sourceRGBBlendFactor = try Self.blend(number(blend, "srcBlend"))
            color.destinationRGBBlendFactor = try Self.blend(number(blend, "destBlend"))
            color.sourceAlphaBlendFactor = try Self.blend(number(blend, "srcBlendAlpha"))
            color.destinationAlphaBlendFactor = try Self.blend(number(blend, "destBlendAlpha"))
            color.rgbBlendOperation = try Self.blendOperation(number(blend, "blendOp"))
            color.alphaBlendOperation = try Self.blendOperation(number(blend, "blendOpAlpha"))
            let write = Int(try number(blend, "colMask"))
            color.writeMask = Self.colorMask(write)
            let depth = MTLDepthStencilDescriptor()
            depth.depthCompareFunction = depthStencilPixelFormat == .stencil8 ? .always : try Self.compare(number(state, "zTest"))
            depth.isDepthWriteEnabled = try number(state, "zWrite") != 0
            let stencil = MTLStencilDescriptor()
            stencil.stencilCompareFunction = try Self.compare(number(stencilOp, "comp"))
            stencil.stencilFailureOperation = try Self.stencil(number(stencilOp, "fail"))
            stencil.depthFailureOperation = try Self.stencil(number(stencilOp, "zFail"))
            stencil.depthStencilPassOperation = try Self.stencil(number(stencilOp, "pass"))
            stencil.readMask = UInt32(try number(state, "stencilReadMask"))
            stencil.writeMask = UInt32(try number(state, "stencilWriteMask"))
            depth.frontFaceStencil = stencil
            depth.backFaceStencil = stencil
            guard let depthState = device.makeDepthStencilState(descriptor: depth) else { throw Failure.message("Cannot create source depth/stencil state") }
            let culling = try number(state, "culling")
            let cull: MTLCullMode = culling == 0 ? .none : culling == 1 ? .front : .back
            // Materials differ in uniforms, textures and depth/stencil state;
            // none changes an otherwise identical immutable render pipeline.
            // Include every nondefault descriptor field used above. Reflection
            // is shared only with that exact shader interface and layout.
            let pipelineKey = SourcePipelineKey(shader: key,
                vertexLayout: attributes.flatMap { [$0.0, Int($0.1.rawValue), $0.2] }
                    + [30, MemoryLayout<Vertex>.stride, Int(MTLVertexStepFunction.perVertex.rawValue)],
                attachmentState: [pipeline.depthAttachmentPixelFormat.rawValue, pipeline.stencilAttachmentPixelFormat.rawValue,
                    color.pixelFormat.rawValue, color.isBlendingEnabled ? 1 : 0,
                    color.sourceRGBBlendFactor.rawValue, color.destinationRGBBlendFactor.rawValue,
                    color.sourceAlphaBlendFactor.rawValue, color.destinationAlphaBlendFactor.rawValue,
                    color.rgbBlendOperation.rawValue, color.alphaBlendOperation.rawValue, color.writeMask.rawValue])
            let pass = Pass(shaderKey: key, pipelineKey: pipelineKey, shader: shader, depth: depthState,
                stencilReference: UInt32(try number(state, "stencilRef")), cull: cull,
                id: (record["id"] as? String ?? name) + "/" + passName,
                pipelineDescriptor: pipeline, depthCompare: depth.depthCompareFunction,
                depthWrite: depth.isDepthWriteEnabled)
            pass.texturePlans = shader.textures.map { binding in
                TexturePlan(name: binding.name, vertex: binding.stage == "vertex", index: binding.index,
                    samplerIndex: binding.sampler_index ?? binding.index,
                    defaultID: textureIDs[binding.name] ?? "__white")
            }
            // Reference fixtures retain eager validation of every source pass.
            // Desktop only prepares programs that its submitted scene uses.
            if runtimeSelection == nil { try prepare(pass: pass, values: values, propertyTypes: propertyTypes, device: device) }
            passes.append(pass)
            }
            let material = Material(values: values, textures: textureIDs, passes: passes)
            materials[name] = material
            materialPropertyTypes[name] = propertyTypes
            if let id = inputs.id {
                materials[id] = material
                materialPropertyTypes[id] = propertyTypes
            }
            if let variants = inputs.clipVariants {
                clipMaterialKeys[name] = variants
                if let id = inputs.id { clipMaterialKeys[id] = variants }
            }
        }
        mark("instances")
        return timings
    }

    /// With clipping enabled, surviving fragment depth is in [0, 1]. When
    /// nothing writes depth and its clear value is 1, Always and LessEqual
    /// accept exactly the same fragments. Preserve all stencil and color
    /// state, and omit only that redundant attachment in the desktop subset.
    /// Unknown or newly depth-dependent source data retains the original path.
    private func desktopDepthIsRedundant(records: [[String: Any]]) throws -> Bool {
        guard runtimeSelection != nil, sceneColorMode == .directLDR,
              !requiresCombinedDepth, clearDepth == 1 else { return false }
        var passCount = 0
        func scalar(_ object: [String: Any], _ key: String) -> Double? {
            ((object[key] as? [String: Any])?["value"] as? NSNumber)?.doubleValue
        }
        for record in records {
            let isMap = (record["shader"] as? [String: Any])?["path_id"] as? String == "505394952752169778"
            guard let passes = record["static_pass_states"] as? [[String: Any]] else { return false }
            for pass in passes {
                if pass["disabled_in_serialized_material"] as? Bool == true { continue }
                guard let name = pass["name"] as? String else { return false }
                if name != "Default" && name != "Default-Stencil-Alpha-Blend" && !(isMap && name == "ForwardOnly") { continue }
                guard let state = pass["state"] as? [String: Any],
                      let stencil = state["stencilOp"] as? [String: Any],
                      scalar(state, "zWrite") == 0,
                      // UI records omit the default clip field; the encoder
                      // explicitly enforces .clip for both attachment paths.
                      (state["zClip"] == nil || scalar(state, "zClip") == 1),
                      [4.0, 8.0].contains(scalar(state, "zTest") ?? .nan),
                      scalar(stencil, "zFail") == 0 else { return false }
                passCount += 1
            }
        }
        guard passCount > 0 else { return false }
        if let metadata { return !metadata.fragmentsWriteDepth }
        return try !Self.fragmentsWriteDepth(shaders: shaders, root: root)
    }

    private static func fragmentsWriteDepth(shaders: [String: Shader], root: URL) throws -> Bool {
        // Check the entire available fragment catalog, including clip/mask
        // variants, rather than only programs reached by the first frame.
        let files = Set(shaders.values.compactMap { $0.stages["fragment"]?.file })
        let depthAttribute = try NSRegularExpression(pattern: #"\[\[\s*depth\b"#)
        for file in files {
            let source = try String(contentsOf: root.appendingPathComponent(file), encoding: .utf8)
            if depthAttribute.firstMatch(in: source, range: NSRange(source.startIndex..., in: source)) != nil { return true }
        }
        return false
    }

    private func selectedIndices(_ selected: [Int]?, count: Int) throws -> [Int] {
        guard let selected else { return Array(0..<count) }
        guard !selected.isEmpty, Set(selected).count == selected.count,
              selected.allSatisfy({ $0 >= 0 && $0 < count }) else {
            throw Failure.message("Invalid Watch runtime catalog selection")
        }
        return selected
    }

    private func functions(for key: String, shader: Shader, device: MTLDevice) throws -> (MTLFunction, MTLFunction) {
        if let cached = shaderFunctions[key] { return cached }
        if let programCache {
            let pair = try Self.programFunctions(for: key, shader: shader, root: root,
                device: device, cache: programCache) { compiledSourceLibraryCount += 1 }
            shaderFunctions[key] = pair
            return pair
        }
        func function(_ stage: Stage?) throws -> MTLFunction {
            guard let stage else { throw Failure.message("Incomplete source shader interface") }
            let source = try String(contentsOf: root.appendingPathComponent(stage.file), encoding: .utf8)
            let library: MTLLibrary
            if let cached = shaderLibraries[source] { library = cached }
            else if let cached = programCache?.access({ $0.libraries[source] }) {
                library = cached
                shaderLibraries[source] = cached
            }
            else {
                library = try device.makeLibrary(source: source, options: nil)
                shaderLibraries[source] = library
                compiledSourceLibraryCount += 1
                programCache?.access { if $0.libraries.count < 32 { $0.libraries[source] = library } }
            }
            guard let function = library.makeFunction(name: stage.function) else {
                throw Failure.message("Translated source entry point unavailable: \(key)")
            }
            return function
        }
        let pair = try (function(shader.stages["vertex"]), function(shader.stages["fragment"]))
        shaderFunctions[key] = pair
        programCache?.access { if $0.functions.count < 32 { $0.functions[key] = pair } }
        return pair
    }

    private func prepare(pass: Pass, values: [String: [Float]],
                         propertyTypes: [String: (type: Int, flags: Int)], device: MTLDevice) throws {
        guard pass.pipeline == nil else { return }
        let functions = try functions(for: pass.shaderKey, shader: pass.shader, device: device)
        pass.pipelineDescriptor.vertexFunction = functions.0
        pass.pipelineDescriptor.fragmentFunction = functions.1
        let prepared: (MTLRenderPipelineState, MTLRenderPipelineReflection)
        if let cached = sourcePipelines[pass.pipelineKey] { prepared = cached }
        else if let cached = programCache?.access({ $0.pipelines[pass.pipelineKey] }) {
            prepared = cached
            sourcePipelines[pass.pipelineKey] = cached
        } else {
            var reflection: MTLRenderPipelineReflection?
            let pipeline = try device.makeRenderPipelineState(descriptor: pass.pipelineDescriptor,
                options: .argumentInfo, reflection: &reflection)
            guard let reflection else { throw Failure.message("Source pipeline reflection unavailable: " + pass.id) }
            prepared = (pipeline, reflection)
            sourcePipelines[pass.pipelineKey] = prepared
            programCache?.access { if $0.pipelines.count < 64 { $0.pipelines[pass.pipelineKey] = prepared } }
            compiledSourcePipelineCount += 1
        }
        pass.uniformByteCounts = try constantBufferLengths(shader: pass.shader, reflection: prepared.1)
        pass.uniformPlans = makeUniformPlans(pass: pass, values: values, propertyTypes: propertyTypes)
        pass.pipeline = prepared.0
    }

    private func makeUniformPlans(pass: Pass, values: [String: [Float]],
                                  propertyTypes: [String: (type: Int, flags: Int)]) -> [UniformPlan] {
        var result: [UniformPlan] = []
        for stageName in ["vertex", "fragment"] {
            guard let stage = pass.shader.stages[stageName] else { continue }
            for uniform in stage.uniforms {
                let fields = uniform.fields.map { field -> UniformFieldPlan in
                    let dynamic: UniformValue
                    if values[field.name] != nil { dynamic = .none }
                    else {
                        switch field.name {
                        case "unity_ObjectToWorld", "ObjectToWorld": dynamic = .world
                        case "unity_MatrixVP": dynamic = .viewProjection
                        case "_NonJitteredViewNoTransProjMatrix": dynamic = .viewNoTranslation
                        case "glstate_matrix_projection", "_ProjMatrix", "_UIProjMatrix": dynamic = .projection
                        case "unity_MatrixInvV", "_InvViewMatrix": dynamic = .inverseView
                        case "_UIProjectionParams": dynamic = .uiProjection
                        case "_WorldSpaceCameraPos_Internal": dynamic = .cameraPosition
                        case "_UITime": dynamic = .uiTime
                        case "_Time": dynamic = .time
                        case "_UIScreenParams": dynamic = .screen
                        case "_RenderPathInjected": dynamic = .renderPath
                        case "_HGFlipX": dynamic = .flipX
                        case "_HGFlipY": dynamic = .flipY
                        default: dynamic = .none
                        }
                    }
                    return UniformFieldPlan(name: field.name, offset: field.offset, value: values[field.name],
                        isColor: propertyTypes[field.name]?.type == 0, dynamic: dynamic)
                }
                let length = pass.uniformByteCounts[stageName]?[uniform.index] ?? uniform.size
                let hasMaterial = fields.contains { $0.value != nil }
                // Camera-only buffers can be shared by all materials using
                // the exact same stage interface. Material values stay local.
                let owner = hasMaterial ? pass.id : stage.file
                result.append(UniformPlan(key: owner + "/" + stageName + "/" + String(uniform.index) + "/" + String(length),
                    vertex: stageName == "vertex", index: uniform.index, byteCount: length,
                    fields: fields, fieldNames: Set(fields.map(\.name)),
                    needsWorld: fields.contains { $0.dynamic == .world },
                    needsCamera: fields.contains { $0.dynamic != .none && $0.dynamic != .world && $0.dynamic != .time && $0.dynamic != .uiTime },
                    needsTime: fields.contains { $0.dynamic == .time || $0.dynamic == .uiTime }))
            }
        }
        return result
    }

    private func constantBufferLengths(shader: Shader, reflection: MTLRenderPipelineReflection) throws -> [String: [Int: Int]] {
        var result: [String: [Int: Int]] = [:]
        for (stageName, stage) in shader.stages {
            let arguments = stageName == "vertex" ? (reflection.vertexArguments ?? []) : (reflection.fragmentArguments ?? [])
            var lengths: [Int: Int] = [:]
            for uniform in stage.uniforms {
                let argument = arguments.first { $0.type == .buffer && $0.index == uniform.index }
                let reflectedSize = argument?.bufferDataSize
                let length = max(uniform.size, reflectedSize ?? 0)
                guard uniform.size > 0, length <= 4096 else {
                    throw Failure.message("Source constant buffer exceeds Metal inline limit: \(stage.file) / \(uniform.name)")
                }
                lengths[uniform.index] = length
                let key = "\(stage.file)/\(stageName)/\(uniform.index)"
                constantBufferABIRecords[key] = ConstantBufferABIRecord(file: stage.file, stage: stageName,
                    name: uniform.name, index: uniform.index, sourceSize: uniform.size,
                    reflectedSize: reflectedSize, uploadedSize: length)
            }
            result[stageName] = lengths
        }
        return result
    }

    private static func addressMode(_ value: Int) -> MTLSamplerAddressMode {
        switch value { case 0: return .repeat; case 2: return .mirrorRepeat; case 3: return .mirrorClampToEdge; default: return .clampToEdge }
    }

    private static func gammaToLinear(_ value: Float) -> Float {
        if value <= 0.04045 { return value / 12.92 }
        if value < 1 { return pow((value + 0.055) / 1.055, 2.4) }
        return pow(value, 2.2)
    }

    private static func linearMaterialValue(_ value: [Float], type: Int, flags: Int) -> [Float] {
        guard type == 0 || flags & 32 != 0 else { return value }
        var result = value
        let count = min(type == 2 || type == 3 ? 1 : 3, result.count)
        for i in 0..<count { result[i] = gammaToLinear(result[i]) }
        return result
    }

    private static func blend(_ value: Float) throws -> MTLBlendFactor {
        switch Int(value) {
        case 0: return .zero; case 1: return .one; case 2: return .destinationColor
        case 3: return .sourceColor; case 4: return .oneMinusDestinationColor
        case 5: return .sourceAlpha; case 6: return .oneMinusSourceColor
        case 7: return .destinationAlpha; case 8: return .oneMinusDestinationAlpha
        case 9: return .sourceAlphaSaturated; case 10: return .oneMinusSourceAlpha
        default: throw Failure.message("Unmapped source blend enum: \(value)")
        }
    }

    private static func blendOperation(_ value: Float) throws -> MTLBlendOperation {
        switch Int(value) {
        case 0: return .add; case 1: return .subtract; case 2: return .reverseSubtract
        case 3: return .min; case 4: return .max
        default: throw Failure.message("Unmapped source blend operation: \(value)")
        }
    }

    private static func compare(_ value: Float) throws -> MTLCompareFunction {
        switch Int(value) {
        case 0, 8: return .always; case 1: return .never; case 2: return .less
        case 3: return .equal; case 4: return .lessEqual; case 5: return .greater
        case 6: return .notEqual; case 7: return .greaterEqual
        default: throw Failure.message("Unmapped source depth/stencil enum: \(value)")
        }
    }

    private static func stencil(_ value: Float) throws -> MTLStencilOperation {
        switch Int(value) {
        case 0: return .keep; case 1: return .zero; case 2: return .replace
        case 3: return .incrementClamp; case 4: return .decrementClamp; case 5: return .invert
        case 6: return .incrementWrap; case 7: return .decrementWrap
        default: throw Failure.message("Unmapped source stencil enum: \(value)")
        }
    }
}
