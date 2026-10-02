import AppKit
import MetalKit
import ImageIO
import CryptoKit
import simd

/// An isolated real source UIImage draw, not a screenshot of the desktop.
/// The selected batch is never rewritten or registered with replacement art.
@main
enum VerifySourcePlateProjection {
    private static let nodeID = HUDSourceID(rawValue: "CAB-194e41a66c2317b9df19269f505210be:9209943895239257303")
    private static let componentID = "CAB-194e41a66c2317b9df19269f505210be:-8375897836268196649"
    private static let materialID = "CAB-311c0d8d29f5c344193835dd71e1e86a:15186476803644734"
    private static let width = 1728, height = 1080

    // Exact informational/unbound messages present in the already captured
    // 1dec2e1 stable source frame. This isolated plate verifies the submitted
    // batch, not complete Domain/account state or all animation bindings.
    // Every other builder diagnostic (including resource/geometry skips) fails.
    private static let knownFrameDiagnostics: Set<String> = [
        "Domain selection: all-declared-source-levels-reference",
        "Current level, player marker, unlock/selection and load completion require explicit caller state.",
        "Level-model wrappers use original deselected endpoints for a reference with no current level.",
        "The source -90 degree world rotation tween targets loadedRegionTransform; its invocation in Watch is not established and is not applied here.",
        "Renderer own UISortingOrder offsets are applied; conflicting ancestor writer lifecycle order remains unverified.",
        "Unity scheduling and pixel-identical engine rendering remain unverified.",
        "Unbound source curve: <unresolved-path-crc32:1680172171>",
        "Unbound source curve: <unresolved-path-crc32:2768072397>",
        "Unbound source curve: Btn/Icon",
        "Unbound source curve: Btn/Icon01",
        "Unbound source curve: Content/WatchNode/canvas_watch/LeftBottomNode/LeftBottomNode01/CharfomationBtnNode",
        "Unbound source curve: ContentNode/Image",
        "Unbound source curve: Icon",
        "Unbound source curve: Line",
        "Unbound source curve: btn_back/Icon"
    ]

    private struct ProjectedVertex {
        let point: SIMD2<Double>
        let inverseW: Double
        let uv: SIMD2<Double>
    }

    private struct AlphaTexture {
        let width: Int
        let height: Int
        let bytes: [UInt8]
        let source: [String: Any]

        func linearClamp(_ uv: SIMD2<Double>) -> Double {
            let x = uv.x * Double(width) - 0.5, y = uv.y * Double(height) - 0.5
            let ix = Int(floor(x)), iy = Int(floor(y))
            let fx = x - Double(ix), fy = y - Double(iy)
            func texel(_ x: Int, _ y: Int) -> Double {
                Double(bytes[min(height - 1, max(0, y)) * width + min(width - 1, max(0, x))]) / 255
            }
            return (texel(ix, iy) * (1 - fx) + texel(ix + 1, iy) * fx) * (1 - fy)
                + (texel(ix, iy + 1) * (1 - fx) + texel(ix + 1, iy + 1) * fx) * fy
        }
    }

    static func main() throws {
        do { try verify() }
        catch {
            if CommandLine.arguments.count == 2 {
                let output = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
                try? FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)
                try? writeJSON(["schemaVersion": 1, "fixtureSucceeded": false, "comparisonPassed": false,
                    "commit": ProcessInfo.processInfo.environment["GITHUB_SHA"] ?? "local",
                    "error": String(describing: error)], to: output.appendingPathComponent("plate-failure.json"))
            }
            throw error
        }
    }

    private static func verify() throws {
        guard CommandLine.arguments.count == 2 else { throw HUDSourceError.invalid("Expected isolated plate output directory") }
        let output = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
        try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)
        let app = NSApplication.shared
        app.setActivationPolicy(.accessory)
        let viewport = CGRect(x: 0, y: 0, width: Double(width), height: Double(height))
        let document = try HUDSourceWatchDocument()
        guard let component = document.components[nodeID]?.first(where: { $0.id.rawValue == componentID }),
              component.kind == "UIImage", component["m_Type"].number == 0,
              component["m_Material"].targetID?.rawValue == materialID else {
            throw HUDSourceError.invalid("Original char UIImage identity changed")
        }
        let runtime = try HUDSourceJSON.decoder().decode(HUDSourceJSONValue.self,
            from: Data(contentsOf: document.root.appendingPathComponent("runtime-root-camera.json")))
        let camera = try HUDSourceWatchCamera(runtimeRoot: runtime)
        let size = SIMD2<Double>(Double(width), Double(height))
        let euler = try camera.gyro.targetEuler(mouseUnity: size / 2, screenSize: size)
        let view = try camera.frame(screenSize: size, localRotation: HUDSourceWatchCamera.quaternion(eulerDegrees: euler))
        let gpuY = HUDSourceGeometry.scale(SIMD3<Double>(1, -1, 1))
        let gpuProjection = HUDSourceGeometry.floatMatrix(simd_mul(gpuY, view.camera.projection))
        let gpu = HUDSourceMetalRenderer.Camera(
            viewProjection: HUDSourceGeometry.floatMatrix(simd_mul(gpuY, view.camera.viewProjection)),
            viewNoTranslationProjection: try HUDSourceWatchCamera.viewNoTranslationProjection(
                gpuProjection: gpuProjection, view: view.camera.view),
            worldSpacePosition: SIMD3(Float(camera.cameraWorld.columns.3.x), Float(camera.cameraWorld.columns.3.y), Float(camera.cameraWorld.columns.3.z)),
            timeSeconds: 0, renderPathInjected: 1, flipX: 0, flipY: 0,
            projection: gpuProjection, inverseView: HUDSourceGeometry.floatMatrix(camera.shaderCameraToWorld),
            uiProjectionParameters: try HUDSourceWatchCamera.uiProjectionParams(gpuProjection: gpuProjection,
                near: Float(camera.near), far: Float(camera.far)))
        let root = document.root.deletingLastPathComponent()
        let renderer = try HUDSourceMetalRenderer(frame: viewport, resourceRoot: root)
        let builder = try HUDSourceWatchFrameBuilder(document: document, renderer: renderer)
        var pose = try document.animation.pose(entranceTime: document.animation.entrance.lastKeyTime,
            ambientTime: 0, exitTime: nil, canvasResolution: view.layout.canvasSize)
        document.applyMacButtonAvailability(to: &pose)
        let frame = try builder.build(pose: pose, worldRoot: view.worldRoot)
        let selected = frame.batches.filter { $0.mesh == "ui/" + componentID && $0.sourceNodeID == nodeID.rawValue }
        let targetPath = document.scene.node(nodeID)?.path ?? ""
        let rootPath = document.scene.node(document.scene.rootID)?.path ?? ""
        let relativeTargetPath = targetPath.hasPrefix(rootPath + "/") ? String(targetPath.dropFirst(rootPath.count + 1)) : targetPath
        let unexpectedDiagnostics = frame.diagnostics.filter { !knownFrameDiagnostics.contains($0) }
        let targetRelatedUnboundPaths = pose.unboundPaths.filter { path in
            // Full/Watch-root-relative named bindings must not refer to the
            // target or an ancestor. The two unresolved CRCs cannot be assigned
            // a semantic node and remain explicitly reported below.
            [targetPath, relativeTargetPath].contains { target in
                !path.hasPrefix("<unresolved-path-crc32:") && (target == path || target.hasPrefix(path + "/"))
            }
        }.sorted()
        let frameDiagnosticReport: [String: Any] = [
            "fullFrameDiagnostics": frame.diagnostics,
            "allowedKnownMessages": frame.diagnostics.filter { knownFrameDiagnostics.contains($0) },
            "unexpectedMessages": unexpectedDiagnostics,
            "targetRelatedNamedUnboundPaths": targetRelatedUnboundPaths,
            "unresolvedPathCRCs": pose.unboundPaths.filter { $0.hasPrefix("<unresolved-path-crc32:") }.sorted(),
            "targetPath": targetPath, "targetRelativePath": relativeTargetPath,
            "targetActive": frame.resolved[nodeID]?.activeInHierarchy == true,
            "targetBatchCount": selected.count,
            "policy": "Only the 15 exact known stable-frame messages are permitted. All other builder diagnostics fail; actual isolated renderer diagnostics must remain empty.",
            "scope": "Known Domain/reference and unbound-curve limitations are retained. This fixture checks the actual submitted plate pipeline and does not establish animation coverage for unresolved bindings."
        ]
        try writeJSON(frameDiagnosticReport, to: output.appendingPathComponent("plate-source-frame-diagnostics.json"))
        guard frame.resolved[nodeID]?.activeInHierarchy == true, selected.count == 1,
              unexpectedDiagnostics.isEmpty, targetRelatedUnboundPaths.isEmpty else {
            throw HUDSourceError.invalid("Original plate missing or source frame has diagnostics: \(frame.diagnostics)")
        }
        let batch = selected[0]
        let window = NSWindow(contentRect: viewport, styleMask: [.borderless], backing: .buffered, defer: false)
        window.isOpaque = false; window.backgroundColor = .clear; window.hasShadow = false
        window.contentView = renderer; window.orderFront(nil)
        defer { window.orderOut(nil) }
        renderer.drawableSize = CGSize(width: Double(width), height: Double(height))
        app.finishLaunching(); window.displayIfNeeded()
        // This is the same registered batch and source pipeline as the full
        // frame. Only other batches are excluded from this diagnostic draw.
        renderer.submit(camera: gpu, batches: [batch])
        renderer.draw()
        let image = try renderer.copyDrawableImage()
        guard renderer.renderedFrameGeneration == renderer.submittedFrameGeneration,
              renderer.diagnostics.isEmpty, let raw = renderer.drawableReadbackBGRA,
              let pixels = renderer.drawableReadbackReport,
              pixels.width == width, pixels.height == height, pixels.rowBytes >= width * 4,
              raw.count == pixels.rowBytes * height else {
            throw HUDSourceError.invalid("Isolated source plate has no fresh complete GPU readback: \(renderer.diagnostics)")
        }
        let geometry = try renderer.previewGeometry(for: batch)
        let passes = try renderer.previewPasses(for: batch)
        try raw.write(to: output.appendingPathComponent("plate-raw.bgra"))
        try geometry.vertexBytes.write(to: output.appendingPathComponent("plate-uploaded.vertices"))
        try geometry.indexBytes.write(to: output.appendingPathComponent("plate.indices"))
        var alpha = [UInt8](repeating: 0, count: width * height)
        for y in 0..<height { for x in 0..<width { alpha[y * width + x] = raw[y * pixels.rowBytes + x * 4 + 3] } }
        try Data(alpha).write(to: output.appendingPathComponent("plate-gpu.alpha8"))
        try writePNG(image, to: output.appendingPathComponent("plate-black-matte.png"))
        try writeGray(alpha, width: width, height: height, to: output.appendingPathComponent("plate-gpu-alpha.png"))
        let projected = try geometry.positions.indices.map { index in
            try project(geometry.positions[index], uv: geometry.uv[index], world: batch.world, camera: gpu)
        }
        let cpu = try geometry.positions.map { p -> [Double] in
            guard let result = view.camera.project(SIMD3(Double(p.x), Double(p.y), Double(p.z)),
                world: HUDSourceGeometry.doubleMatrix(batch.world), viewport: viewport) else {
                throw HUDSourceError.invalid("Source CPU plate projection failed")
            }
            return [Double(result.point.x), Double(result.point.y)]
        }
        let independentPoints = projected.map { [$0.point.x, $0.point.y] }
        let cpuDifference = zip(independentPoints, cpu).map { hypot($0.0[0] - $0.1[0], $0.0[1] - $0.1[1]) }.max() ?? .infinity
        guard cpuDifference.isFinite, cpuDifference < 0.01 else {
            throw HUDSourceError.invalid("CPU and injected uniform pair differ by \(cpuDifference)px")
        }
        var report: [String: Any] = ["schemaVersion": 1,
            "commit": ProcessInfo.processInfo.environment["GITHUB_SHA"] ?? "local",
            "captureSucceeded": true, "comparisonRequired": true, "comparisonExecuted": false, "comparisonPassed": false,
            "sourceFrameDiagnostics": frameDiagnosticReport,
            "scope": "Actual isolated original char UIImage GPU alpha against independent projection/sampling; no recording registration or camera fit",
            "limits": ["Checks shader/vertex/uniform/texture/viewport glue, not whether mesh generation or live recording camera agrees with the game.",
                "The real source batch is isolated on transparent direct-LDR clear; source HDR composite/bloom and neighboring batches are deliberately absent.",
                "CPU source texture oracle is independently decoded original mip alpha; hardware BC7 decode and filter rounding use the stated fixed byte tolerance."],
            "target": ["nodeID": nodeID.rawValue, "componentID": componentID, "materialID": batch.material,
                "path": targetPath, "mesh": batch.mesh],
            "width": width, "height": height, "rawRowBytes": pixels.rowBytes,
            "rowOrder": "Actual Metal texture memory y0=top; raw alpha extracted without transfer or matte",
            "batch": ["worldColumns": matrix(batch.world), "color": vector(batch.color),
                "uniformOverrides": batch.uniformOverrides.mapValues { $0.map(Double.init) },
                "textureOverrides": batch.textureOverrides, "stencilOverridePresent": batch.stencilOverrides != nil],
            "camera": ["sourceGyroEuler": [euler.x, euler.y, euler.z], "runtimeFOV": view.layout.runtimeVerticalFieldOfViewDegrees,
                "canvasSize": [view.layout.canvasSize.x, view.layout.canvasSize.y], "worldScale": view.layout.scale,
                "gpuViewProjectionColumns": matrix(gpu.viewProjection),
                "gpuViewNoTranslationProjectionColumns": matrix(gpu.viewNoTranslationProjection),
                "worldSpacePosition": [Double(gpu.worldSpacePosition.x), Double(gpu.worldSpacePosition.y), Double(gpu.worldSpacePosition.z)],
                "perPassTuple": [Double(gpu.renderPathInjected), Double(gpu.flipX), Double(gpu.flipY), 0]],
            "actualGeometry": ["vertexStride": geometry.vertexStride, "vertexOffsets": geometry.vertexOffsets,
                "positions": geometry.positions.map(vector), "uv": geometry.uv.map { [Double($0.x), Double($0.y)] },
                "registeredColors": geometry.originalColors.map(vector), "uploadedColors": geometry.uploadedColors.map(vector),
                "indices": geometry.indices.map(Int.init), "indexRange": batch.indexRange.map { [$0.lowerBound, $0.upperBound] } ?? [0, geometry.indices.count]],
            "projection": ["independentShaderPixels": independentPoints, "cpuSourcePixels": cpu,
                "clipInverseW": projected.map(\.inverseW), "maxPairDifferencePixels": cpuDifference],
            "passes": passes.map { pass -> [String: Any] in
                ["id": pass.id, "shader": pass.shader, "vertexFile": pass.vertexFile, "fragmentFile": pass.fragmentFile,
                 "materialValues": pass.materialValues.mapValues { $0.map(Double.init) }, "textures": pass.textures,
                 "alphaBlend": pass.alphaBlend, "vertexAttributes": pass.vertexAttributes]
            }]
        let reasons = simplePathReasons(batch: batch, geometry: geometry, passes: passes)
        report["simplePathRejectedReasons"] = reasons
        try writeJSON(report, to: output.appendingPathComponent("plate-projection-report.json"))
        if reasons.isEmpty, let textureID = batch.textureOverrides["_MainTex"] {
            let texture = try loadAlphaTexture(id: textureID, document: document, root: root)
            let colorAlpha = Double(geometry.uploadedColors[0].w)
            let vertexAlpha = (colorAlpha * 255).rounded(.toNearestOrEven) / 255
            let expected = try rasterize(projected, indices: geometry.indices, texture: texture, alpha: vertexAlpha)
            try Data(expected).write(to: output.appendingPathComponent("plate-cpu.alpha8"))
            try writeGray(expected, width: width, height: height, to: output.appendingPathComponent("plate-cpu-alpha.png"))
            let comparison = compare(actual: alpha, expected: expected, projected: projected, maximumAlpha: vertexAlpha)
            report["sourceAlpha"] = texture.source
            report["vertexAlphaAfterSourceRint"] = vertexAlpha
            report["comparisonExecuted"] = true
            report["comparisonPassed"] = comparison["passed"]
            report["comparison"] = comparison
            try writeJSON(report, to: output.appendingPathComponent("plate-projection-report.json"))
            guard comparison["passed"] as? Bool == true else {
                throw HUDSourceError.invalid("Original isolated plate GPU/CPU alpha mismatch; see plate-projection-report.json")
            }
            print("Source plate projection: actual GPU alpha comparison passed")
        } else {
            report["status"] = "diagnostic-only: actual shader/batch does not satisfy the proven simple alpha contract"
            try writeJSON(report, to: output.appendingPathComponent("plate-projection-report.json"))
            throw HUDSourceError.invalid("Required original program194 comparison unavailable; diagnostics preserved: " + reasons.joined(separator: "; "))
        }
    }

    private static func simplePathReasons(batch: HUDSourceMetalRenderer.Batch,
        geometry: HUDSourceMetalRenderer.PreviewGeometry, passes: [HUDSourceMetalRenderer.PreviewPass]) -> [String] {
        var reasons: [String] = []
        if batch.material != materialID { reasons.append("Derived/other material") }
        if batch.stencilOverrides != nil || batch.indexRange != nil || batch.colorWriteMask != nil { reasons.append("Dynamic stencil/index/write-mask override") }
        if geometry.positions.count != 4 || geometry.indices != [0, 1, 2, 2, 3, 0] { reasons.append("Not the original simple quad") }
        if let first = geometry.uploadedColors.first {
            if geometry.uploadedColors.dropFirst().contains(where: { $0 != first }) { reasons.append("Nonconstant vertex color") }
            if !first.w.isFinite || first.w <= 0 || first.w > 1 { reasons.append("Unbounded/empty vertex alpha") }
        } else { reasons.append("Empty vertex color stream") }
        if passes.count != 1 { reasons.append("Not one source pass"); return reasons }
        let pass = passes[0]
        if pass.shader != "HGRP/UI/Default" || !pass.vertexFile.hasSuffix("program194.vertex.metal")
            || !pass.fragmentFile.hasSuffix("program194.fragment.metal") { reasons.append("Unproven source shader program") }
        if pass.alphaBlend["enabled"] != 1 || pass.alphaBlend["source"] != Int(MTLBlendFactor.one.rawValue)
            || pass.alphaBlend["destination"] != Int(MTLBlendFactor.oneMinusSourceAlpha.rawValue)
            || pass.alphaBlend["operation"] != Int(MTLBlendOperation.add.rawValue)
            || pass.alphaBlend["writeMask"] != Int(MTLColorWriteMask.all.rawValue) { reasons.append("Unproven alpha blend") }
        for key in ["_UIImageOpaque", "_UseAdditiveBlendMode", "_UIVFXParameters"] {
            if pass.materialValues[key] != [0] { reasons.append("Active/unknown " + key) }
        }
        if pass.materialValues["_Color"] != [1, 1, 1, 1] { reasons.append("Material color not unity") }
        if pass.materialValues["_StencilComp"] != [8] || pass.materialValues["_ZTestUI"] != [8] { reasons.append("Non-Always stencil/depth test") }
        if pass.materialValues["_MainTex_ST"] != [1, 1, 0, 0] { reasons.append("Texture UV transform") }
        if let addition = pass.materialValues["textureSampleAdd"], addition != [0, 0, 0, 0] { reasons.append("Texture sample addition") }
        if pass.textures.keys.sorted() != ["_MainTex"] || pass.textures["_MainTex"] != batch.textureOverrides["_MainTex"] { reasons.append("Other texture dependency") }
        let expectedOffsets = [geometry.vertexOffsets["position"] ?? -1, geometry.vertexOffsets["color"] ?? -1, geometry.vertexOffsets["uv"] ?? -1]
        let expectedFormats = [MTLVertexFormat.float4, .float4, .float2]
        for index in 0...2 {
            guard let attribute = pass.vertexAttributes.first(where: { $0["attribute"] == index }),
                  expectedOffsets[index] >= 0, attribute["offset"] == expectedOffsets[index],
                  attribute["format"] == Int(expectedFormats[index].rawValue),
                  attribute["bufferIndex"] == 30, attribute["stride"] == geometry.vertexStride else {
                reasons.append("Unproven input ABI attribute \(index)"); continue
            }
        }
        return reasons
    }

    private static func project(_ p: SIMD4<Float>, uv: SIMD2<Float>, world: simd_float4x4,
        camera: HUDSourceMetalRenderer.Camera) throws -> ProjectedVertex {
        // Independent scalar projection from the actual uploaded Float values.
        // The camera-relative tuple and final Y negate are source program194.
        let point = simd_mul(HUDSourceGeometry.doubleMatrix(world), SIMD4(Double(p.x), Double(p.y), Double(p.z), 1))
        let relative = SIMD4(point.x - Double(camera.worldSpacePosition.x), point.y - Double(camera.worldSpacePosition.y),
            point.z - Double(camera.worldSpacePosition.z), 1)
        var clip = simd_mul(HUDSourceGeometry.doubleMatrix(camera.viewNoTranslationProjection), relative)
        clip.y = -clip.y
        guard [clip.x, clip.y, clip.z, clip.w].allSatisfy(\.isFinite), clip.w > 0,
              clip.z / clip.w >= 0, clip.z / clip.w <= 1 else { throw HUDSourceError.invalid("Invalid source plate clip coordinates") }
        return ProjectedVertex(point: SIMD2((clip.x / clip.w + 1) * Double(width) / 2,
            (1 - clip.y / clip.w) * Double(height) / 2), inverseW: 1 / clip.w, uv: SIMD2(Double(uv.x), Double(uv.y)))
    }

    private static func loadAlphaTexture(id: String, document: HUDSourceWatchDocument, root: URL) throws -> AlphaTexture {
        let records = try HUDSourceJSON.decoder().decode(HUDSourceJSONValue.self, from: Data(contentsOf: root.appendingPathComponent("textures.json")))
        let matched = records.array.filter { $0["texture_id"].string == id || $0["path_id"].string == id }
        guard matched.count == 1 else { throw HUDSourceError.invalid("Ambiguous original alpha texture") }
        let record = matched[0], sampler = record["sampler"]
        guard record["texture_format"].number == 25, record["mip_count"].number == 1,
              sampler["m_FilterMode"].number == 1, sampler["m_Aniso"].number == 1,
              sampler["m_WrapU"].number == 1, sampler["m_WrapV"].number == 1,
              let file = record["decoded_mips_file"].string, let sourceSHA = record["decoded_mips_sha256"].string else {
            throw HUDSourceError.invalid("Original alpha oracle requires authored one-mip BC7, linear/clamp/no-aniso sampler")
        }
        let w = Int(record["width"].float()), h = Int(record["height"].float())
        let data = try Data(contentsOf: root.appendingPathComponent(file))
        guard w > 0, h > 0, data.count == w * h * 4, digest(data) == sourceSHA else {
            throw HUDSourceError.invalid("Original decoded alpha mip bytes/hash differ")
        }
        let alpha = (0..<(w * h)).map { data[$0 * 4 + 3] }
        guard let metadata = document.sprites["source_textures"].array.first(where: { $0["id"].string == id }),
              let png = metadata["png"]["file"].string, let pngSHA = metadata["png"]["sha256"].string else {
            throw HUDSourceError.invalid("Original full texture PNG provenance unavailable")
        }
        let pngData = try Data(contentsOf: document.root.appendingPathComponent(png))
        guard digest(pngData) == pngSHA, let source = CGImageSourceCreateWithData(pngData as CFData, nil),
              let image = CGImageSourceCreateImageAtIndex(source, 0, nil), image.width == w, image.height == h else {
            throw HUDSourceError.invalid("Original PNG alpha/dimensions/hash differ")
        }
        var pixels = Data(repeating: 0, count: w * h * 4)
        try pixels.withUnsafeMutableBytes { (buffer: UnsafeMutableRawBufferPointer) in
            guard let context = CGContext(data: buffer.baseAddress, width: w, height: h, bitsPerComponent: 8,
                bytesPerRow: w * 4, space: CGColorSpaceCreateDeviceRGB(),
                bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue | CGBitmapInfo.byteOrder32Big.rawValue) else {
                throw HUDSourceError.invalid("Cannot independently decode PNG alpha")
            }
            context.interpolationQuality = .none
            context.setBlendMode(.copy)
            context.draw(image, in: CGRect(x: 0, y: 0, width: Double(w), height: Double(h)))
        }
        let pngAlpha = (0..<(w * h)).map { pixels[$0 * 4 + 3] }
        let reverse = (0..<h).flatMap { Array(pngAlpha[((h - 1 - $0) * w)..<((h - $0) * w)]) }
        let directEqual = pngAlpha == alpha, reverseEqual = reverse == alpha
        guard directEqual != reverseEqual else {
            throw HUDSourceError.invalid("PNG-to-authored-mip alpha row mapping is missing or ambiguous")
        }
        return AlphaTexture(width: w, height: h, bytes: alpha, source: ["textureID": id, "width": w, "height": h,
            "decodedMipFile": file, "decodedMipSHA256": sourceSHA, "sourcePNG": png, "sourcePNGSHA256": pngSHA,
            "pngAlphaEqualsAuthoredMip": true, "cgBitmapRowsToAuthoredMip": directEqual ? "identity" : "reverseRows",
            "sampleCoordinates": "Unchanged source normalized UV; authored uploaded mip memory row0, texel center uv=(i+.5)/size; bilinear clamp",
            "alphaTransfer": "No sRGB transfer on alpha", "mipCount": 1])
    }

    private static func rasterize(_ vertices: [ProjectedVertex], indices: [UInt32], texture: AlphaTexture, alpha: Double) throws -> [UInt8] {
        guard vertices.count == 4, indices.count == 6 else { throw HUDSourceError.invalid("Expected original quad") }
        var result = [UInt8](repeating: 0, count: width * height)
        let minX = max(0, Int(floor(vertices.map { $0.point.x }.min()!)) - 1)
        let maxX = min(width - 1, Int(ceil(vertices.map { $0.point.x }.max()!)) + 1)
        let minY = max(0, Int(floor(vertices.map { $0.point.y }.min()!)) - 1)
        let maxY = min(height - 1, Int(ceil(vertices.map { $0.point.y }.max()!)) + 1)
        guard minX <= maxX, minY <= maxY else { throw HUDSourceError.invalid("Plate is offscreen") }
        for y in minY...maxY { for x in minX...maxX {
            let pixel = SIMD2(Double(x) + .5, Double(y) + .5)
            for offset in stride(from: 0, to: indices.count, by: 3) {
                let a = vertices[Int(indices[offset])], b = vertices[Int(indices[offset + 1])], c = vertices[Int(indices[offset + 2])]
                let denominator = cross(b.point - a.point, c.point - a.point)
                guard denominator != 0 else { throw HUDSourceError.invalid("Degenerate source triangle") }
                let wb = cross(pixel - a.point, c.point - a.point) / denominator
                let wc = cross(b.point - a.point, pixel - a.point) / denominator
                let wa = 1 - wb - wc
                guard wa >= 0, wb >= 0, wc >= 0 else { continue }
                let weights = SIMD3(wa * a.inverseW, wb * b.inverseW, wc * c.inverseW)
                let uv = (a.uv * weights.x + b.uv * weights.y + c.uv * weights.z) / (weights.x + weights.y + weights.z)
                let sampled = texture.linearClamp(uv) * alpha
                result[y * width + x] = UInt8(min(255, max(0, (sampled * 255).rounded(.toNearestOrEven))))
                break
            }
        } }
        return result
    }

    private static func compare(actual: [UInt8], expected: [UInt8], projected: [ProjectedVertex], maximumAlpha: Double) -> [String: Any] {
        // Fixed before first native execution: 4 alpha-byte allowance for
        // original BC7 decode/filter/UNORM rounding. Exclude only the 1.5px
        // outer raster boundary; all interior Sprite alpha edges stay tested.
        let tolerance = 4, threshold = max(1, Int((maximumAlpha * 255 * .5).rounded()))
        var coreCount = 0, coreBad = 0, maximumCoreError = 0, maximumError = 0
        var contourBad = 0, actualNonzero = 0, expectedNonzero = 0, squaredError = 0.0
        func nearby(_ mask: [UInt8], x: Int, y: Int) -> Bool {
            for dy in -1...1 { for dx in -1...1 {
                let xx = x + dx, yy = y + dy
                if xx >= 0, yy >= 0, xx < width, yy < height, Int(mask[yy * width + xx]) >= threshold { return true }
            } }
            return false
        }
        for y in 0..<height { for x in 0..<width {
            let index = y * width + x, difference = abs(Int(actual[index]) - Int(expected[index]))
            maximumError = max(maximumError, difference); squaredError += Double(difference * difference)
            if actual[index] > 0 { actualNonzero += 1 }; if expected[index] > 0 { expectedNonzero += 1 }
            let point = SIMD2(Double(x) + .5, Double(y) + .5)
            let distance = (0..<4).map { edge -> Double in
                let a = projected[edge].point, b = projected[(edge + 1) % 4].point
                let delta = b - a
                let t = min(1, max(0, simd_dot(point - a, delta) / simd_dot(delta, delta)))
                return simd_length(point - (a + delta * t))
            }.min() ?? 0
            if distance >= 1.5 {
                coreCount += 1; maximumCoreError = max(maximumCoreError, difference)
                if difference > tolerance { coreBad += 1 }
            }
            if Int(actual[index]) >= threshold, !nearby(expected, x: x, y: y) { contourBad += 1 }
            if Int(expected[index]) >= threshold, !nearby(actual, x: x, y: y) { contourBad += 1 }
        } }
        return ["passed": coreBad == 0 && contourBad == 0 && actualNonzero > 1000 && expectedNonzero > 1000,
            "coreComparedPixels": coreCount, "coreAlphaToleranceBytes": tolerance,
            "corePixelsOverTolerance": coreBad, "maxCoreAlphaByteError": maximumCoreError,
            "maxAllPixelAlphaByteError": maximumError, "rmsAllPixelAlphaByteError": sqrt(squaredError / Double(actual.count)),
            "outerRasterBoundaryExcludedPixels": actual.count - coreCount, "outerRasterBoundaryExclusionPixels": 1.5,
            "contourAlphaThresholdByte": threshold, "contourNeighbourTolerancePixels": 1,
            "bidirectionalContourPixelsOutsideOnePixelNeighbour": contourBad,
            "actualNonzeroAlphaPixels": actualNonzero, "expectedNonzeroAlphaPixels": expectedNonzero,
            "scope": "Full raw alpha plane; every interior source alpha edge compared. Only outer quad rasterization boundary excluded from byte check, still covered by bidirectional threshold-mask neighbour check."]
    }

    private static func cross(_ a: SIMD2<Double>, _ b: SIMD2<Double>) -> Double { a.x * b.y - a.y * b.x }
    private static func vector(_ value: SIMD4<Float>) -> [Double] { [Double(value.x), Double(value.y), Double(value.z), Double(value.w)] }
    private static func matrix(_ value: simd_float4x4) -> [[Double]] { (0..<4).map { vector(value[$0]) } }
    private static func digest(_ data: Data) -> String { SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined() }
    private static func writeJSON(_ value: [String: Any], to url: URL) throws {
        try JSONSerialization.data(withJSONObject: value, options: [.prettyPrinted, .sortedKeys]).write(to: url)
    }
    private static func writePNG(_ image: CGImage, to url: URL) throws {
        guard let destination = CGImageDestinationCreateWithURL(url as CFURL, "public.png" as CFString, 1, nil) else {
            throw HUDSourceError.invalid("Cannot create isolated plate PNG")
        }
        CGImageDestinationAddImage(destination, image, nil)
        guard CGImageDestinationFinalize(destination) else { throw HUDSourceError.invalid("Cannot finish isolated plate PNG") }
    }
    private static func writeGray(_ values: [UInt8], width: Int, height: Int, to url: URL) throws {
        guard let provider = CGDataProvider(data: Data(values) as CFData),
              let image = CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 8,
                bytesPerRow: width, space: CGColorSpaceCreateDeviceGray(), bitmapInfo: [], provider: provider,
                decode: nil, shouldInterpolate: false, intent: .defaultIntent) else {
            throw HUDSourceError.invalid("Cannot construct raw alpha visualization")
        }
        try writePNG(image, to: url)
    }
}
