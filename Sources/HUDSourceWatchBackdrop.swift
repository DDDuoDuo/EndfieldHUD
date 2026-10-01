import CoreGraphics
import Foundation
import MetalKit
import simd

/// Desktop pixels are an explicit input adapter to the original WatchBlur draw.
/// This class neither captures a screen nor requests screen-capture permission.
final class HUDSourceWatchBackdrop {
    struct TileReport: Encodable {
        let displayID: UInt32
        let sourcePixelSize: [Int]
        let capturedAppKitRect: [Double]
        let requestedAppKitRect: [Double]
        let sourcePixelsPerAppKitPoint: [Double]
        let destinationPixelRect: [Double]
        let sourceColorSpaceName: String?
        let sourceColorSpaceModel: Int
        let sourceBitsPerComponent: Int
        let sourceBitsPerPixel: Int
    }

    struct Report: Encodable {
        let inputMode: String
        let hudRect: [Double]
        let outputPixelSize: [Int]
        let outputPixelsPerAppKitPoint: [Double]
        let outputColorSpace: String
        let renderingIntent: String
        let interpolation: String
        let textureOrigin: String
        let sourceUVPlatformMapping: String
        let sourceMaterialID: String
        let tiles: [TileReport]
    }

    struct Composite {
        let image: CGImage
        let report: Report
    }

    private let renderer: HUDSourceMetalRenderer
    private let device: MTLDevice
    private let queue: MTLCommandQueue
    private let frosted: HUDSourceFrostedGlass
    private let materialID: String
    private let sourceNodeID: String
    private let sourceVertexTint: SIMD4<Float>
    private let geometryName = "__watch-backdrop-raw-image"
    private let textureName = "__watch-backdrop"
    private var ready = false
    private(set) var report: Report?

    init(renderer: HUDSourceMetalRenderer, resourceRoot: URL) throws {
        guard let device = renderer.device, let queue = device.makeCommandQueue() else {
            throw HUDSourceError.invalid("WatchBlur Metal device/queue unavailable")
        }
        let root = resourceRoot.appendingPathComponent("HDR/WatchBlur")
        func object(_ file: String) throws -> [String: Any] {
            guard let value = try JSONSerialization.jsonObject(with: Data(contentsOf: root.appendingPathComponent(file))) as? [String: Any] else {
                throw HUDSourceError.invalid("Original WatchBlur contract is malformed: " + file)
            }
            return value
        }
        let contract = try object("material-contract.json")
        let runtime = try object("material-runtime.json")
        let scene = try object("scene.json")
        guard let material = contract["material"] as? [String: Any], let materialID = material["id"] as? String,
              runtime["id"] as? String == materialID,
              let geometry = contract["geometry"] as? [String: Any],
              let componentID = geometry["component_id"] as? String,
              geometry["component_type"] as? String == "UnityEngine.UI.RawImage",
              let textureBinding = contract["texture_binding"] as? [String: Any],
              textureBinding["property"] as? String == "_MainTex",
              (textureBinding["requires_dynamic_capture"] as? NSNumber)?.boolValue == true,
              let passes = runtime["static_pass_states"] as? [[String: Any]],
              passes.compactMap({ ($0["source_pass_index"] as? NSNumber)?.intValue }) == [0, 1],
              let records = scene["objects"] as? [[String: Any]],
              let raw = records.first(where: { $0["id"] as? String == componentID }),
              raw["script"] as? String == "RawImage", let rawData = raw["data"] as? [String: Any] else {
            throw HUDSourceError.invalid("Original WatchBlur RawImage/material contract unavailable")
        }
        func pathID(_ pointer: Any?) -> String? {
            guard let pointer = pointer as? [String: Any] else { return nil }
            // Preserve signed path IDs through NSNumber, never through a JSON Double.
            return pointer["m_PathID"] as? String ?? (pointer["m_PathID"] as? NSNumber)?.stringValue
        }
        guard let gameObjectID = pathID(rawData["m_GameObject"]),
              let rect = records.first(where: { record in
                  guard record["type"] as? String == "RectTransform", let data = record["data"] as? [String: Any] else { return false }
                  return pathID(data["m_GameObject"]) == gameObjectID
              }), let rectID = rect["id"] as? String, let rectData = rect["data"] as? [String: Any],
              let rawColor = rawData["m_Color"] as? [String: Any],
              let uvRect = rawData["m_UVRect"] as? [String: Any],
              let canvas = records.first(where: { $0["type"] as? String == "Canvas" }),
              let canvasData = canvas["data"] as? [String: Any],
              let gammaFlag = canvasData["m_VertexColorAlwaysGammaSpace"] as? NSNumber,
              let graphicMaterialID = pathID(rawData["m_Material"]),
              materialID.hasSuffix(":" + graphicMaterialID) else {
            throw HUDSourceError.invalid("Original WatchBlur geometry/color data unavailable")
        }
        func value(_ dictionary: [String: Any], _ field: String) throws -> Double {
            guard let number = dictionary[field] as? NSNumber, number.doubleValue.isFinite else {
                throw HUDSourceError.invalid("Original WatchBlur numeric field unavailable: " + field)
            }
            return number.doubleValue
        }
        func vector(_ name: String, expected: [String: Double]) throws {
            guard let vector = rectData[name] as? [String: Any] else { throw HUDSourceError.invalid("Original WatchBlur rect field unavailable: " + name) }
            for (axis, expectedValue) in expected {
                guard try value(vector, axis) == expectedValue else { throw HUDSourceError.invalid("Original WatchBlur full-screen anchors/offsets differ") }
            }
        }
        try vector("m_AnchorMin", expected: ["x": 0, "y": 0])
        try vector("m_AnchorMax", expected: ["x": 1, "y": 1])
        try vector("m_AnchoredPosition", expected: ["x": 0, "y": 0])
        try vector("m_SizeDelta", expected: ["x": 0, "y": 0])
        try vector("m_Pivot", expected: ["x": 0.5, "y": 0.5])
        try vector("m_LocalPosition", expected: ["x": 0, "y": 0, "z": 0])
        try vector("m_LocalScale", expected: ["x": 1, "y": 1, "z": 1])
        guard try value(uvRect, "x") == 0, try value(uvRect, "y") == 0,
              try value(uvRect, "width") == 1, try value(uvRect, "height") == 1 else {
            throw HUDSourceError.invalid("Original WatchBlur UV rect differs")
        }
        var color = SIMD4<Float>(repeating: 0)
        for (axis, suffix) in ["r", "g", "b", "a"].enumerated() {
            let original = Float(try value(rawColor, suffix))
            color[axis] = (min(1, max(0, original)) * 255).rounded(.toNearestOrEven) / 255
        }
        guard color == SIMD4<Float>(76 / 255, 76 / 255, 76 / 255, 1), !gammaFlag.boolValue else {
            throw HUDSourceError.invalid("Original WatchBlur Color32/Canvas gamma policy differs")
        }
        // Same original Linear-project Canvas vertex rule as the ordinary source UI.
        sourceVertexTint = HUDSourceWatchFrameBuilder.canvasVertexColor(color, alwaysGamma: gammaFlag.boolValue)
        self.renderer = renderer
        self.device = device
        self.queue = queue
        self.materialID = materialID
        sourceNodeID = rectID
        frosted = try HUDSourceFrostedGlass(device: device, resourceRoot: resourceRoot, mode: .desktopDisplayPixels)
        queue.label = "WatchBlur desktop-display input"
        // Stretch anchors and zero offsets fill this clip-space canvas independently of
        // Watch's world camera/gyro. Source UVRect remains 0..1; only the texture API
        // origin changes v to 1-v for top-left desktop/capture textures.
        try renderer.registerGeometry(named: geometryName,
            positions: [SIMD4(-1, -1, 0, 1), SIMD4(1, -1, 0, 1), SIMD4(1, 1, 0, 1), SIMD4(-1, 1, 0, 1)],
            uv: [SIMD2(0, 1), SIMD2(1, 1), SIMD2(1, 0), SIMD2(0, 0)], indices: [0, 1, 2, 2, 3, 0])
    }

    /// One explicit display-to-drawable resample, with ICC conversion per source image.
    /// No capture APIs are called, so synthetic profiled tiles can exercise this in GPU CI.
    static func compositeDesktop(frame: HUDSourceDesktopBackdrop.Frame, drawableSize: CGSize,
                                 sourceMaterialID: String) throws -> Composite {
        let hud = frame.requestedRect
        func valid(_ rect: CGRect) -> Bool {
            !rect.isNull && !rect.isInfinite && rect.minX.isFinite && rect.minY.isFinite &&
                rect.width.isFinite && rect.height.isFinite && rect.width > 0 && rect.height > 0 &&
                rect.maxX.isFinite && rect.maxY.isFinite
        }
        guard valid(hud), drawableSize.width.isFinite, drawableSize.height.isFinite,
              let width = Int(exactly: drawableSize.width), width > 0,
              let height = Int(exactly: drawableSize.height), height > 0,
              width <= Int.max / 4, height <= Int.max / (width * 4), !frame.tiles.isEmpty,
              let sRGB = CGColorSpace(name: CGColorSpace.sRGB),
              let context = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8,
                                      bytesPerRow: width * 4, space: sRGB,
                                      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue | CGBitmapInfo.byteOrder32Big.rawValue) else {
            throw HUDSourceError.invalid("Cannot allocate a valid WatchBlur sRGB desktop raster")
        }
        let sx = CGFloat(width) / hud.width, sy = CGFloat(height) / hud.height
        func target(_ rect: CGRect) -> CGRect {
            CGRect(x: (rect.minX - hud.minX) * sx, y: (rect.minY - hud.minY) * sy,
                   width: rect.width * sx, height: rect.height * sy)
        }
        var coverage: [CGRect] = []
        var area: CGFloat = 0
        for tile in frame.tiles {
            guard valid(tile.requestedAppKitRect), valid(tile.capturedAppKitRect),
                  hud.contains(tile.requestedAppKitRect), tile.capturedAppKitRect.contains(tile.requestedAppKitRect),
                  tile.image.width > 0, tile.image.height > 0, tile.image.colorSpace != nil else {
                throw HUDSourceError.invalid("WatchBlur desktop tile has incomplete geometry/profile")
            }
            for previous in coverage {
                let overlap = previous.intersection(tile.requestedAppKitRect)
                guard overlap.isNull || overlap.width <= 0 || overlap.height <= 0 else {
                    throw HUDSourceError.invalid("WatchBlur desktop tile coverage overlaps")
                }
            }
            coverage.append(tile.requestedAppKitRect)
            area += tile.requestedAppKitRect.width * tile.requestedAppKitRect.height
        }
        // An uncovered monitor gap must not become an invented black source for _UIImageOpaque.
        let expectedArea = hud.width * hud.height
        guard abs(area - expectedArea) <= max(0.000001, expectedArea * 0.000000001) else {
            throw HUDSourceError.invalid("WatchBlur desktop tiles do not cover the complete HUD rectangle")
        }
        context.setRenderingIntent(.relativeColorimetric)
        context.interpolationQuality = .high
        context.setBlendMode(.copy)
        context.setShouldAntialias(false)
        context.clear(CGRect(x: 0, y: 0, width: CGFloat(width), height: CGFloat(height)))
        // Quartz bitmap drawing uses +Y-up placement, as do the AppKit input rectangles.
        // The resulting CGImage is uploaded with an explicit top-left Metal origin.
        var reports: [TileReport] = []
        for tile in frame.tiles {
            let destination = target(tile.capturedAppKitRect)
            context.saveGState()
            context.clip(to: target(tile.requestedAppKitRect))
            context.draw(tile.image, in: destination)
            context.restoreGState()
            let profile = tile.image.colorSpace!
            reports.append(TileReport(displayID: tile.displayID, sourcePixelSize: [tile.image.width, tile.image.height],
                capturedAppKitRect: rectangle(tile.capturedAppKitRect), requestedAppKitRect: rectangle(tile.requestedAppKitRect),
                sourcePixelsPerAppKitPoint: [Double(CGFloat(tile.image.width) / tile.capturedAppKitRect.width),
                                            Double(CGFloat(tile.image.height) / tile.capturedAppKitRect.height)],
                destinationPixelRect: rectangle(destination), sourceColorSpaceName: profile.name.map { $0 as String },
                sourceColorSpaceModel: Int(profile.model.rawValue), sourceBitsPerComponent: tile.image.bitsPerComponent,
                sourceBitsPerPixel: tile.image.bitsPerPixel))
        }
        guard let image = context.makeImage() else { throw HUDSourceError.invalid("Cannot create WatchBlur desktop composite") }
        return Composite(image: image, report: Report(inputMode: "desktopDisplayPixels", hudRect: rectangle(hud),
            outputPixelSize: [width, height], outputPixelsPerAppKitPoint: [Double(sx), Double(sy)],
            outputColorSpace: "sRGB", renderingIntent: "relativeColorimetric", interpolation: "CoreGraphics high",
            textureOrigin: "Metal topLeft", sourceUVPlatformMapping: "u unchanged; v = 1 - sourceRawImageUV.y",
            sourceMaterialID: sourceMaterialID, tiles: reports))
    }

    /// Only expose a completed source texture. Failed preparation never supplies a stale batch.
    func prepare(frame: HUDSourceDesktopBackdrop.Frame, drawableSize: CGSize) throws {
        ready = false
        report = nil
        let composite = try Self.compositeDesktop(frame: frame, drawableSize: drawableSize, sourceMaterialID: materialID)
        let input = try MTKTextureLoader(device: device).newTexture(cgImage: composite.image, options: [
            .origin: MTKTextureLoader.Origin.topLeft.rawValue, .SRGB: NSNumber(value: true),
            .generateMipmaps: NSNumber(value: false),
            .textureUsage: NSNumber(value: MTLTextureUsage.shaderRead.rawValue)
        ])
        guard input.width == composite.image.width, input.height == composite.image.height,
              input.pixelFormat == .rgba8Unorm_srgb || input.pixelFormat == .bgra8Unorm_srgb,
              let command = queue.makeCommandBuffer() else {
            throw HUDSourceError.invalid("WatchBlur desktop upload must retain a single-mip sRGB raster")
        }
        command.label = "Original WatchBlur FrostedGlass/extraction"
        let capture = try frosted.encode(command: command, input: input, outputSize: drawableSize)
        command.commit()
        command.waitUntilCompleted()
        guard command.status == .completed, command.error == nil,
              capture.pixelFormat == .rgba8Unorm_srgb, capture.mipmapLevelCount == 1,
              capture.width == input.width, capture.height == input.height else {
            throw HUDSourceError.invalid("WatchBlur source GPU preparation failed: " + String(describing: command.error))
        }
        // Original UIBlurRT sampler: Point, Repeat, no mips. The source shader,
        // its saved colors, and both enabled passes remain renderer-owned.
        try renderer.registerTexture(named: textureName, texture: capture, filterMode: 0, wrapU: 0, wrapV: 0)
        try renderer.enableSourceRGBHDR()
        report = composite.report
        ready = true
    }

    func batch(alpha: Float) -> HUDSourceMetalRenderer.Batch? {
        guard ready else { return nil }
        let opacity = alpha.isFinite ? min(1, max(0, alpha)) : 0
        var tint = sourceVertexTint
        // Animation supplies CanvasGroup alpha after the Graphic's Color32 RGB/alpha stage.
        tint.w *= opacity
        let gpuY: [Float] = [1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
        return HUDSourceMetalRenderer.Batch(mesh: geometryName, material: materialID, world: matrix_identity_float4x4,
            color: tint, uniformOverrides: ["unity_MatrixVP": gpuY, "_NonJitteredViewNoTransProjMatrix": gpuY,
                "_WorldSpaceCameraPos_Internal": [0, 0, 0, 0], "_RenderPathInjected": [1], "_HGFlipX": [0], "_HGFlipY": [0]],
            textureOverrides: ["_MainTex": textureName], sourceNodeID: sourceNodeID)
    }

    private static func rectangle(_ rect: CGRect) -> [Double] {
        [Double(rect.minX), Double(rect.minY), Double(rect.width), Double(rect.height)]
    }
}
