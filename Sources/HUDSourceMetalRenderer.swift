import AppKit
import MetalKit
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

    private struct Material {
        var values: [String: [Float]]
        var textures: [String: String]
        var passes: [Pass]
    }

    private struct Pass {
        var shader: Shader
        var pipeline: MTLRenderPipelineState
        var uniformByteCounts: [String: [Int: Int]]
        var depth: MTLDepthStencilState
        var stencilReference: UInt32
        var cull: MTLCullMode
        var id: String
        var pipelineDescriptor: MTLRenderPipelineDescriptor
        var depthCompare: MTLCompareFunction
        var depthWrite: Bool
    }

    private struct TextureAsset {
        var texture: MTLTexture
        var sampler: MTLSamplerState
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
    private var geometries: [String: Geometry] = [:]
    private var materials: [String: Material] = [:]
    private var clipMaterialKeys: [String: [String: String]] = [:]
    private var materialPropertyTypes: [String: [String: (type: Int, flags: Int)]] = [:]
    private var textureAssets: [String: TextureAsset] = [:]
    private var camera: Camera?
    private var batches: [Batch] = []
    private var lastDrawable: CAMetalDrawable?
    private var lastRenderCommand: MTLCommandBuffer?
    private(set) var submittedFrameGeneration: UInt64 = 0
    private(set) var renderedFrameGeneration: UInt64?
    private(set) var drawableReadbackReport: HUDSourceDrawableReadback.Report?
    private(set) var drawableReadbackBGRA: Data?
    private var tintedVertices: [String: (color: SIMD4<Float>, buffer: MTLBuffer)] = [:]
    private struct StencilKey: Hashable { var pass: String; var state: StencilState }
    private var stencilStates: [StencilKey: MTLDepthStencilState] = [:]
    private var colorPipelines: [String: MTLRenderPipelineState] = [:]
    private struct MaterialSet {
        var materials: [String: Material]
        var propertyTypes: [String: [String: (type: Int, flags: Int)]]
        var clipKeys: [String: [String: String]]
        var abi: [String: ConstantBufferABIRecord]
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

    init(frame: CGRect, resourceRoot: URL? = nil, sceneColorMode: SceneColorMode = .directLDR) throws {
        guard let device = MTLCreateSystemDefaultDevice(), let queue = device.makeCommandQueue() else {
            throw Failure.message("Metal device or command queue unavailable")
        }
        guard let root = resourceRoot ?? HUDResources.url(for: "WatchSource") else {
            throw Failure.message("WatchSource resource directory unavailable")
        }
        self.queue = queue
        self.root = root
        self.sceneColorMode = sceneColorMode
        let colorPolicy = try JSONSerialization.jsonObject(with: Data(contentsOf: root.appendingPathComponent("render-color-policy.json"))) as? [String: Any]
        guard (colorPolicy?["serialized_color_space"] as? Int) == 1 else {
            throw Failure.message("Original linear project color-space evidence unavailable")
        }
        var catalog: [String: Shader] = [:]
        for (key, file) in [("fx", "fx-shader.json"), ("image", "image-shader.json"), ("imageStencil", "image-stencil-shader.json"),
                            ("imageMainFX", "image-mainfx-shader.json"), ("imageDissolveFX", "image-dissolvefx-shader.json"),
                            ("imageAlphaClip", "image-alphaclip-shader.json"), ("imageClipRect", "image-cliprect-shader.json"),
                            ("imageClipRectAlpha", "image-cliprect-alphaclip-shader.json"),
                            ("font", "font-shader.json"), ("fontUnderlay", "font-underlay-shader.json")] {
            catalog[key] = try JSONDecoder().decode(Shader.self, from: Data(contentsOf: root.appendingPathComponent(file)))
        }
        for program in 12...17 {
            catalog["map\(program)"] = try JSONDecoder().decode(Shader.self, from: Data(contentsOf: root.appendingPathComponent("map-\(program)-shader.json")))
        }
        for program in [12, 13] {
            catalog["fx\(program)"] = try JSONDecoder().decode(Shader.self, from: Data(contentsOf: root.appendingPathComponent("fx-\(program)-shader.json")))
        }
        catalog["imageWorld"] = try JSONDecoder().decode(Shader.self, from: Data(contentsOf: root.appendingPathComponent("image-world-shader.json")))
        for (key, stem) in [("imageMainFX", "image-mainfx"), ("imageDissolveFX", "image-dissolvefx"),
                            ("imageWorld", "image-world"), ("imageStencil", "image-stencil"),
                            ("font", "font"), ("fontUnderlay", "font-underlay")] {
            for (suffix, file) in [("AlphaClip", "alphaclip"), ("ClipRect", "cliprect"), ("ClipRectAlpha", "cliprect-alphaclip")] {
                catalog[key + suffix] = try JSONDecoder().decode(Shader.self, from: Data(contentsOf: root.appendingPathComponent(stem + "-" + file + "-shader.json")))
            }
        }
        for (key, stem) in [("imageSoftMask", "image-softmask"), ("imageMainFXSoftMask", "image-mainfx-softmask"),
                            ("imageDissolveFXSoftMask", "image-dissolvefx-softmask"), ("imageWorldSoftMask", "image-world-softmask"),
                            ("imageStencilSoftMask", "image-stencil-softmask"), ("fontSoftMask", "font-softmask"),
                            ("fontUnderlaySoftMask", "font-underlay-softmask")] {
            for (suffix, file) in [("", ""), ("AlphaClip", "-alphaclip"), ("ClipRect", "-cliprect"), ("ClipRectAlpha", "-cliprect-alphaclip")] {
                catalog[key + suffix] = try JSONDecoder().decode(Shader.self, from: Data(contentsOf: root.appendingPathComponent(stem + file + "-shader.json")))
            }
        }
        shaders = catalog
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
        isPaused = true
        enableSetNeedsDisplay = true
        delegate = self
        if sceneColorMode == .sourceRGBHDR {
            uiComposite = try HUDSourceUIComposite(device: device, resourceRoot: root, outputPixelFormat: colorPixelFormat)
        }
        try loadGeometries(device: device)
        try loadTextures(device: device)
        try loadFontTextures()
        try loadMaterials(device: device)
    }

    required init(coder: NSCoder) { fatalError("Use init(frame:resourceRoot:)") }

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
        guard let vertexBuffer = vertices.withUnsafeBufferPointer({ device.makeBuffer(bytes: $0.baseAddress!, length: $0.count * MemoryLayout<Vertex>.stride, options: .storageModeShared) }),
              let indexBuffer = indices.withUnsafeBufferPointer({ device.makeBuffer(bytes: $0.baseAddress!, length: $0.count * MemoryLayout<UInt32>.stride, options: .storageModeShared) }) else {
            throw Failure.message("Cannot allocate registered source geometry: \(name)")
        }
        geometries[name] = Geometry(vertices: vertexBuffer, indices: indexBuffer, indexCount: indices.count, originalVertices: vertices,
                                    hasTextChannels: !normals.isEmpty && !uv1.isEmpty)
        tintedVertices.removeValue(forKey: name)
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
    }

    func containsTexture(named name: String) -> Bool { textureAssets[name] != nil }

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
            clipKeys: clipMaterialKeys, abi: constantBufferABIRecords)
        materialSets[previousMode] = previous
        let previousColors = colorPipelines, previousStencil = stencilStates
        sceneColorMode = mode
        colorPipelines.removeAll(); stencilStates.removeAll()
        if let cached = materialSets[mode] {
            materials = cached.materials; materialPropertyTypes = cached.propertyTypes
            clipMaterialKeys = cached.clipKeys; constantBufferABIRecords = cached.abi
            if mode == .directLDR { sceneColorTexture = nil }
            return
        }
        do {
            materials.removeAll(); materialPropertyTypes.removeAll(); clipMaterialKeys.removeAll()
            constantBufferABIRecords.removeAll()
            try loadMaterials(device: device)
            materialSets[mode] = MaterialSet(materials: materials, propertyTypes: materialPropertyTypes,
                clipKeys: clipMaterialKeys, abi: constantBufferABIRecords)
            if mode == .directLDR { sceneColorTexture = nil }
        } catch {
            sceneColorMode = previousMode
            materials = previous.materials; materialPropertyTypes = previous.propertyTypes
            clipMaterialKeys = previous.clipKeys; constantBufferABIRecords = previous.abi
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

    func draw(in view: MTKView) {
        guard let camera, let display = currentRenderPassDescriptor,
              let drawable = currentDrawable, let command = queue.makeCommandBuffer() else { return }
        diagnostics.removeAll(keepingCapacity: true)
        let descriptor: MTLRenderPassDescriptor
        do { descriptor = try sceneDescriptor(display: display, drawable: drawable, command: command) }
        catch { diagnostics.append(String(describing: error)); return }
        guard let encoder = command.makeRenderCommandEncoder(descriptor: descriptor) else {
            diagnostics.append("Cannot encode source UI scene"); return
        }
        for batch in batches {
            guard let geometry = geometries[batch.mesh], let material = materials[batch.material] else {
                diagnostics.append("Unsupported source mesh/material: \(batch.mesh) / \(batch.material)")
                continue
            }
            if material.passes.contains(where: { $0.shader.shader == "HGRP/UI/TextMeshPro/Distance Field" }),
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
            if material.passes.contains(where: { $0.shader.shader == "HGRP/MinimapTerrain" }),
               batch.uniformOverrides["_WatchWorldToLocalMatrix"] == nil {
                diagnostics.append("Source map requires _WatchWorldToLocalMatrix: \(batch.mesh)")
                continue
            }
            if material.passes.contains(where: { $0.shader.textures.contains(where: { $0.name == "_SoftMaskTex" }) }) {
                guard batch.uniformOverrides["_WorldToSoftMask"]?.count == 16,
                      batch.uniformOverrides["_SoftMaskTex_ST"]?.count == 4,
                      let maskTexture = batch.textureOverrides["_SoftMaskTex"], textureAssets[maskTexture] != nil else {
                    diagnostics.append("Source soft mask requires its Canvas-local matrix, sprite ST and original texture: \(batch.mesh)")
                    continue
                }
            }
            let needsInverseView = material.passes.contains { pass in
                pass.shader.stages.values.contains { stage in
                    stage.uniforms.contains { uniform in
                        uniform.fields.contains { $0.name == "unity_MatrixInvV" || $0.name == "_InvViewMatrix" }
                    }
                }
            }
            if needsInverseView, camera.inverseView == nil,
               batch.uniformOverrides["unity_MatrixInvV"] == nil || batch.uniformOverrides["_InvViewMatrix"] == nil {
                diagnostics.append("Source shader requires inverse camera view: \(batch.mesh)")
                continue
            }
            let needsUIProjection = material.passes.contains { pass in
                pass.shader.stages.values.contains { stage in
                    stage.uniforms.contains { uniform in uniform.fields.contains { $0.name == "_UIProjectionParams" } }
                }
            }
            if needsUIProjection, camera.uiProjectionParameters == nil,
               batch.uniformOverrides["_UIProjectionParams"] == nil {
                diagnostics.append("Source shader requires HG UI projection parameters: \(batch.mesh)")
                continue
            }
            // Source vertices use their original UVs. Missing Unity mesh color
            // channels have the engine's white default, multiplied by the
            // explicit scene vertex tint, not by the shader material tint.
            let vertexBuffer: MTLBuffer
            if batch.color == SIMD4<Float>(repeating: 1) {
                vertexBuffer = geometry.vertices
            } else if let cached = tintedVertices[batch.mesh], cached.color == batch.color {
                vertexBuffer = cached.buffer
            } else {
                var vertices = geometry.originalVertices
                for i in vertices.indices { vertices[i].color *= batch.color }
                guard let device, let buffer = vertices.withUnsafeBufferPointer({ raw in
                    device.makeBuffer(bytes: raw.baseAddress!, length: raw.count * MemoryLayout<Vertex>.stride, options: .storageModeShared)
                }) else {
                    diagnostics.append("Cannot allocate source vertex tint: \(batch.mesh)")
                    continue
                }
                // At most one tint buffer per registered geometry; unchanged
                // frames reuse it rather than regenerate source artwork.
                tintedVertices[batch.mesh] = (batch.color, buffer)
                vertexBuffer = buffer
            }
            encoder.setVertexBuffer(vertexBuffer, offset: 0, index: 30)
            for pass in material.passes {
            let requiredTextures = pass.shader.textures.map { binding in
                (binding, batch.textureOverrides[binding.name] ?? material.textures[binding.name] ?? "__white")
            }
            let missing = requiredTextures.map { $0.1 }.filter { textureAssets[$0] == nil }
            guard missing.isEmpty else {
                diagnostics.append("Missing source textures: " + missing.joined(separator: ", "))
                continue
            }
            do {
                encoder.setRenderPipelineState(try colorPipeline(pass: pass, mask: batch.colorWriteMask))
                encoder.setDepthStencilState(try depthState(pass: pass, override: batch.stencilOverrides))
            } catch {
                diagnostics.append(String(describing: error))
                continue
            }
            encoder.setStencilReferenceValue(batch.stencilOverrides?.reference ?? pass.stencilReference)
            encoder.setCullMode(pass.cull)
            for stageName in ["vertex", "fragment"] {
                guard let stage = pass.shader.stages[stageName] else { continue }
                for uniform in stage.uniforms {
                    // SPIR-V's last occupied byte can precede MSL struct tail
                    // padding. Use the compiled argument's actual byte size;
                    // member offsets and original source values stay intact.
                    let byteCount = pass.uniformByteCounts[stageName]?[uniform.index] ?? uniform.size
                    var bytes = Data(repeating: 0, count: byteCount)
                    for field in uniform.fields {
                        if let override = batch.uniformOverrides[field.name] {
                            Self.put(override, into: &bytes, at: field.offset)
                        } else if let values = material.values[field.name] {
                            Self.put(values, into: &bytes, at: field.offset)
                        } else {
                            switch field.name {
                            case "unity_ObjectToWorld", "ObjectToWorld": Self.put(batch.world, into: &bytes, at: field.offset)
                            case "unity_MatrixVP":
                                Self.put(camera.viewProjection, into: &bytes, at: field.offset)
                            case "_NonJitteredViewNoTransProjMatrix":
                                Self.put(camera.viewNoTranslationProjection, into: &bytes, at: field.offset)
                            case "glstate_matrix_projection", "_ProjMatrix", "_UIProjMatrix":
                                if let projection = camera.projection { Self.put(projection, into: &bytes, at: field.offset) }
                            case "unity_MatrixInvV", "_InvViewMatrix":
                                if let inverseView = camera.inverseView { Self.put(inverseView, into: &bytes, at: field.offset) }
                            case "_UIProjectionParams":
                                if let value = camera.uiProjectionParameters {
                                    Self.put([value.x, value.y, value.z, value.w], into: &bytes, at: field.offset)
                                }
                            case "_WorldSpaceCameraPos_Internal":
                                Self.put([camera.worldSpacePosition.x, camera.worldSpacePosition.y, camera.worldSpacePosition.z, 0], into: &bytes, at: field.offset)
                            case "_UITime":
                                // HG copies its render-time tuple to UI globals.
                                // This adapter has no game's gameplay clock;
                                // the selected programs consume render time.
                                Self.put([camera.timeSeconds * 0.05, camera.timeSeconds, camera.timeSeconds * 2, 0], into: &bytes, at: field.offset)
                            case "_Time":
                                Self.put([camera.timeSeconds / 20, camera.timeSeconds, camera.timeSeconds * 2, camera.timeSeconds * 3], into: &bytes, at: field.offset)
                            case "_UIScreenParams":
                                let width = Float(drawableSize.width), height = Float(drawableSize.height)
                                // UpdateUIShaderVariablesGlobal copies HG's
                                // _ScreenSize, whose zw are reciprocal size.
                                if width > 0, height > 0 { Self.put([width, height, 1 / width, 1 / height], into: &bytes, at: field.offset) }
                            case "_RenderPathInjected": Self.put([camera.renderPathInjected], into: &bytes, at: field.offset)
                            case "_HGFlipX": Self.put([camera.flipX], into: &bytes, at: field.offset)
                            case "_HGFlipY": Self.put([camera.flipY], into: &bytes, at: field.offset)
                            default: break
                            }
                        }
                    }
                    bytes.withUnsafeBytes { raw in
                        guard let address = raw.baseAddress else { return }
                        if stageName == "vertex" { encoder.setVertexBytes(address, length: bytes.count, index: uniform.index) }
                        else { encoder.setFragmentBytes(address, length: bytes.count, index: uniform.index) }
                    }
                }
            }
            for (binding, id) in requiredTextures {
                guard let asset = textureAssets[id] else { continue }
                if binding.stage == "vertex" {
                    encoder.setVertexTexture(asset.texture, index: binding.index)
                    encoder.setVertexSamplerState(asset.sampler, index: binding.sampler_index ?? binding.index)
                } else {
                    encoder.setFragmentTexture(asset.texture, index: binding.index)
                    encoder.setFragmentSamplerState(asset.sampler, index: binding.sampler_index ?? binding.index)
                }
            }
            encoder.drawIndexedPrimitives(type: .triangle, indexCount: indexRange.count,
                                          indexType: .uint32, indexBuffer: geometry.indices,
                                          indexBufferOffset: indexRange.lowerBound * MemoryLayout<UInt32>.size)
            }
        }
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
        command.present(drawable)
        command.commit()
    }

    private static func put(_ values: [Float], into data: inout Data, at offset: Int) {
        guard offset >= 0, offset + values.count * MemoryLayout<Float>.size <= data.count else { return }
        values.withUnsafeBufferPointer { values in
            let bytes = UnsafeRawBufferPointer(values)
            data.replaceSubrange(offset..<(offset + bytes.count), with: bytes)
        }
    }

    private func colorPipeline(pass: Pass, mask: UInt8?) throws -> MTLRenderPipelineState {
        guard let mask else { return pass.pipeline }
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
        try JSONSerialization.jsonObject(with: Data(contentsOf: root.appendingPathComponent(name)))
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
        for info in textures {
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
            let data = try Data(contentsOf: root.appendingPathComponent(selectedFile))
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
            textureAssets[id] = TextureAsset(texture: texture, sampler: state)
            if let cab = info["cab"] as? String { textureAssets[cab + ":" + id] = TextureAsset(texture: texture, sampler: state) }
        }
        let descriptor = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .rgba8Unorm, width: 1, height: 1, mipmapped: false)
        guard let white = device.makeTexture(descriptor: descriptor), let sampler = device.makeSamplerState(descriptor: MTLSamplerDescriptor()) else {
            throw Failure.message("Cannot create shader's default white texture")
        }
        let pixel: [UInt8] = [255, 255, 255, 255]
        pixel.withUnsafeBufferPointer { white.replace(region: MTLRegionMake2D(0, 0, 1, 1), mipmapLevel: 0, withBytes: $0.baseAddress!, bytesPerRow: 4) }
        textureAssets["__white"] = TextureAsset(texture: white, sampler: sampler)
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

    private func loadMaterials(device: MTLDevice) throws {
        guard var records = try object("materials.json") as? [[String: Any]] else { throw Failure.message("Invalid source shader/material metadata") }
        if sceneColorMode == .sourceRGBHDR {
            let url = root.appendingPathComponent("HDR/WatchBlur/material-runtime.json")
            if FileManager.default.fileExists(atPath: url.path) {
                guard let record = try JSONSerialization.jsonObject(with: Data(contentsOf: url)) as? [String: Any] else {
                    throw Failure.message("Original WatchBlur material unavailable")
                }
                records.append(record)
            }
        }
        var functions: [String: (MTLFunction, MTLFunction)] = [:]
        for (key, shader) in shaders {
            guard let vertex = shader.stages["vertex"], let fragment = shader.stages["fragment"] else { throw Failure.message("Incomplete source shader interface") }
            let vertexLibrary = try device.makeLibrary(source: String(contentsOf: root.appendingPathComponent(vertex.file), encoding: .utf8), options: nil)
            let fragmentLibrary = try device.makeLibrary(source: String(contentsOf: root.appendingPathComponent(fragment.file), encoding: .utf8), options: nil)
            guard let vertexFunction = vertexLibrary.makeFunction(name: vertex.function), let fragmentFunction = fragmentLibrary.makeFunction(name: fragment.function) else {
                throw Failure.message("Translated source entry point unavailable: \(key)")
            }
            functions[key] = (vertexFunction, fragmentFunction)
        }
        for record in records {
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
                if let value = values[key] { values[key] = Self.linearMaterialValue(value, type: type, flags: flags) }
            }
            var textureIDs: [String: String] = [:]
            for binding in record["texture_bindings"] as? [[String: Any]] ?? [] {
                guard let slot = binding["slot"] as? String,
                      let scale = binding["scale"] as? [String: NSNumber], let offset = binding["offset"] as? [String: NSNumber] else { continue }
                values[slot + "_ST"] = [scale["x"]?.floatValue ?? 1, scale["y"]?.floatValue ?? 1, offset["x"]?.floatValue ?? 0, offset["y"]?.floatValue ?? 0]
                if let texture = binding["texture"] as? [String: Any], let id = texture["path_id"] as? String { textureIDs[slot] = id }
            }
            let sourceShader = record["shader"] as? [String: Any]
            let isFX = (sourceShader?["path_id"] as? String) == "-7864008769510089003"
            let isFont = (sourceShader?["path_id"] as? String) == "2786552470741801451"
            let isMap = (sourceShader?["path_id"] as? String) == "505394952752169778"
            let keywords = Set(serialized["m_ValidKeywords"] as? [String] ?? [])
            var passes: [Pass] = []
            for sourcePass in record["static_pass_states"] as? [[String: Any]] ?? [] {
            if (sourcePass["disabled_in_serialized_material"] as? Bool) == true { continue }
            guard let passName = sourcePass["name"] as? String, let state = sourcePass["state"] as? [String: Any] else { throw Failure.message("Incomplete source pass") }
            if passName != "Default" && passName != "Default-Stencil-Alpha-Blend" && !(isMap && passName == "ForwardOnly") { continue }
            let key: String
            if isMap {
                if keywords.contains("_USE_CONTOUR") { key = "map13" }
                else if keywords.contains("_USE_BUILDING") { key = "map14" }
                else if keywords.contains("_USE_POINTCLOUD") { key = "map16" }
                else if keywords.contains("_USE_OUTLINE") { key = keywords.contains("_ALPHATEST_ON") ? "map17" : "map15" }
                else { key = "map12" }
            }
            else if isFX { key = keywords.contains("HG_UI_VFX_DISSOLVE") ? "fx" : keywords.contains("HG_UI_VFX_MASKTEX") ? "fx13" : "fx12" }
            else if isFont {
                let base = (keywords.contains("UNDERLAY_ON") ? "fontUnderlay" : "font") + (keywords.contains("HG_SOFT_MASKABLE") ? "SoftMask" : "")
                let clip = keywords.contains("UNITY_UI_CLIP_RECT"), alpha = keywords.contains("UNITY_UI_ALPHACLIP")
                key = base + (clip ? (alpha ? "ClipRectAlpha" : "ClipRect") : (alpha ? "AlphaClip" : ""))
            }
            else {
                let base: String
                if passName == "Default-Stencil-Alpha-Blend" { base = "imageStencil" }
                else if keywords.contains("HG_UI_VFX_DISSOLVE") { base = "imageDissolveFX" }
                else if keywords.contains("HG_UI_VFX_MAINTEX") { base = "imageMainFX" }
                else if keywords.contains("HG_WORLD_UI") { base = "imageWorld" }
                else { base = "image" }
                let clip = keywords.contains("UNITY_UI_CLIP_RECT"), alpha = keywords.contains("UNITY_UI_ALPHACLIP")
                key = base + (keywords.contains("HG_SOFT_MASKABLE") ? "SoftMask" : "") + (clip ? (alpha ? "ClipRectAlpha" : "ClipRect") : (alpha ? "AlphaClip" : ""))
            }
            guard let shader = shaders[key], let (vertexFunction, fragmentFunction) = functions[key],
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
            pipeline.vertexFunction = vertexFunction
            pipeline.fragmentFunction = fragmentFunction
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
            pipeline.depthAttachmentPixelFormat = depthStencilPixelFormat
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
            depth.depthCompareFunction = try Self.compare(number(state, "zTest"))
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
            var reflection: MTLRenderPipelineReflection?
            let pipelineState = try device.makeRenderPipelineState(descriptor: pipeline, options: .argumentInfo, reflection: &reflection)
            guard let reflection else { throw Failure.message("Source pipeline reflection unavailable: " + name) }
            let uniformByteCounts = try constantBufferLengths(shader: shader, reflection: reflection)
            passes.append(Pass(shader: shader, pipeline: pipelineState, uniformByteCounts: uniformByteCounts, depth: depthState,
                               stencilReference: UInt32(try number(state, "stencilRef")), cull: cull,
                               id: (record["id"] as? String ?? name) + "/" + passName,
                               pipelineDescriptor: pipeline, depthCompare: depth.depthCompareFunction, depthWrite: depth.isDepthWriteEnabled))
            }
            materials[name] = Material(values: values, textures: textureIDs, passes: passes)
            materialPropertyTypes[name] = propertyTypes
            if let id = record["id"] as? String {
                materials[id] = Material(values: values, textures: textureIDs, passes: passes)
                materialPropertyTypes[id] = propertyTypes
            }
            if let variants = record["clip_variants"] as? [String: String] {
                clipMaterialKeys[name] = variants
                if let id = record["id"] as? String { clipMaterialKeys[id] = variants }
            }
        }
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
