// Oracle for Settings > Display > Center logo on Windows
// (windows/modules/display_center_logo.*): the unchanged Mac
// HUDCenterLogoPresentation.image / displaySize and
// HUDSourceProfileArtwork.texturePixels over the three bundled preset
// wordmarks and one generated synthetic bitmap (also generated, byte for
// byte, by windows/tests/display_center_logo_tests.cpp).
import AppKit
import CryptoKit

func hex(_ bytes: [UInt8]) -> String { bytes.map { String(format: "%02x", $0) }.joined() }
func sha(_ data: Data) -> String { SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined() }

// Straight RGBA8, top-left rows: transparent margins, a translucent band,
// a below-threshold speck (alpha 8, not visible) and a just-visible one (9).
func syntheticRGBA() -> (width: Int, height: Int, rgba: [UInt8]) {
    let width = 96, height = 64
    var rgba = [UInt8](repeating: 0, count: width * height * 4)
    for y in 0..<height { for x in 0..<width {
        let i = (y * width + x) * 4
        if x >= 12 && x < 80 && y >= 10 && y < 50 {
            rgba[i] = UInt8((x * 3) & 255); rgba[i + 1] = UInt8((y * 5) & 255); rgba[i + 2] = 128
            rgba[i + 3] = x < 20 ? 128 : 255
        }
    } }
    rgba[(60 * width + 3) * 4 + 3] = 8; rgba[(60 * width + 3) * 4] = 255
    rgba[(55 * width + 85) * 4 + 3] = 9; rgba[(55 * width + 85) * 4 + 1] = 255
    return (width, height, rgba)
}

func straightImage(width: Int, height: Int, rgba: [UInt8]) -> CGImage {
    let provider = CGDataProvider(data: Data(rgba) as CFData)!
    return CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: width * 4,
        space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.last.rawValue),
        provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent)!
}

func row(name: String, source: CGImage, full: Bool, step: Int) throws -> [String: Any] {
    guard let bitmap = HUDCenterLogoPresentation.image(source) else { fatalError("No visible artwork for \(name)") }
    let display = HUDCenterLogoPresentation.displaySize(for: bitmap)
    let pixels = try HUDSourceProfileArtwork.texturePixels(bitmap)
    let bytes = [UInt8](pixels.rgba)
    var result: [String: Any] = ["name": name, "width": pixels.width, "height": pixels.height,
        "displayWidth": Double(display.width), "displayHeight": Double(display.height),
        "texturePixelsSHA256": sha(pixels.rgba), "sampleStep": full ? 1 : step]
    // texturePixels rows are bottom-origin; samples keep that order.
    var samples: [UInt8] = []
    for y in Swift.stride(from: 0, to: pixels.height, by: full ? 1 : step) {
        for x in Swift.stride(from: 0, to: pixels.width, by: full ? 1 : step) {
            let i = (y * pixels.width + x) * 4
            samples.append(contentsOf: bytes[i..<(i + 4)])
        }
    }
    result["samples"] = hex(samples)
    return result
}

@main struct Export {
    static func main() throws {
        guard CommandLine.arguments.count == 3 else { fatalError("Pass the source Resources directory and a new output JSON") }
        let resources = URL(fileURLWithPath: CommandLine.arguments[1])
        let output = URL(fileURLWithPath: CommandLine.arguments[2])
        guard !FileManager.default.fileExists(atPath: output.path) else { fatalError("Output must be new") }
        var rows: [[String: Any]] = []
        let synthetic = syntheticRGBA()
        rows.append(try row(name: "synthetic", source: straightImage(width: synthetic.width, height: synthetic.height, rgba: synthetic.rgba), full: true, step: 1))
        var assets: [String: String] = [:]
        for name in ["rhodesIsland", "babel", "rhineLab"] {
            let url = resources.appendingPathComponent("AppIconSources/CenterLogos/\(name).png")
            let data = try Data(contentsOf: url)
            assets["Resources/AppIconSources/CenterLogos/\(name).png"] = sha(data)
            // HUDCenterLogo.image: the bundled preset through ImageIO's thumbnail
            // (768 px maximum, transform applied), as the source loads it.
            guard let source = CGImageSourceCreateWithURL(url as CFURL, [kCGImageSourceShouldCache: false] as CFDictionary),
                  let image = CGImageSourceCreateThumbnailAtIndex(source, 0, [
                    kCGImageSourceCreateThumbnailFromImageAlways: true,
                    kCGImageSourceCreateThumbnailWithTransform: true,
                    kCGImageSourceThumbnailMaxPixelSize: 768,
                    kCGImageSourceShouldCacheImmediately: true] as CFDictionary) else { fatalError("Cannot decode \(name)") }
            var preset = try row(name: name, source: image, full: false, step: 4)
            preset["sourceWidth"] = image.width; preset["sourceHeight"] = image.height
            rows.append(preset)
        }
        let result: [String: Any] = [
            "schema": 1, "sourceCommit": "ca04f142185c7de40acd8523bdb563195d90a1d1", "sourceAssets": assets,
            "method": "Unchanged HUDCenterLogoPresentation.image/displaySize and HUDSourceProfileArtwork.texturePixels; the synthetic bitmap is a straight sRGB CGImage",
            "cases": rows]
        let data = try JSONSerialization.data(withJSONObject: result, options: [.prettyPrinted, .sortedKeys])
        try data.write(to: output, options: .withoutOverwriting)
    }
}
