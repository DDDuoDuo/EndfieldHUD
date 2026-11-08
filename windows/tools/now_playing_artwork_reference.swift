// Oracle for the Now Playing small-cover decode on Windows
// (nowPlayingSmallCover in windows/native/now_playing_image.*, over
// windows/modules/artwork_scaling.*):
// the unchanged Mac NowPlayingArtworkLoader.layout / decode /
// trimmingLetterbox (Sources/NowPlayingArtwork.swift) and
// HUDPortraitArtwork.scaledImage (Sources/HUDPortraitArtwork.swift) over
// synthetic straight sRGB pictures that windows/tests/now_playing_artwork_tests.cpp
// builds byte for byte (`picture` below). Each picture is encoded as a PNG and
// decoded through NowPlayingArtworkLoader.decode at the named slot.
import AppKit
import CryptoKit
import ImageIO

// Only the loader's key needs these source types; no decode path reads them.
struct NowPlayingApplication { let bundleIdentifier: String; let pid: Int32 }
struct NowPlayingTrack {
    let identifier: String?; let title: String; let artist: String; let album: String
    let duration: Double?; let artworkRevision: String?
}
enum NowPlayingCatalog {
    static func coverPixels(forDrawn pixels: Int) -> Int { pixels }
    static func coverURL(_ value: String, pixels: Int) -> URL? { URL(string: value) }
}
// HUDPortraitArtwork's frame and layer helpers (never called here) name these.
enum HUDResources { static func url(for name: String) -> URL? { nil } }
enum HUDSourceResourceData { static func read(_ url: URL) throws -> Data { Data() } }
struct UserProfile { let avatarZoom: Double; let avatarOffsetX: Double; let avatarOffsetY: Double }

func hex(_ bytes: [UInt8]) -> String { bytes.map { String(format: "%02x", $0) }.joined() }

// The synthetic pictures (straight RGBA8, top-left rows). Kept in step with
// `picture()` in windows/tests/now_playing_artwork_tests.cpp.
func picture(_ kind: String, _ width: Int, _ height: Int, bar: Int = 0, barBottom: Int = -1,
             barValue: UInt8 = 0, barAlpha: UInt8 = 255) -> [UInt8] {
    var rgba = [UInt8](repeating: 255, count: width * height * 4)
    let bottom = barBottom < 0 ? bar : barBottom
    for y in 0..<height { for x in 0..<width {
        let i = (y * width + x) * 4
        var c: (UInt8, UInt8, UInt8)
        switch kind {
        case "uniform128": c = (128, 128, 128)
        case "uniform255": c = (255, 255, 255)
        case "step": c = x < width / 2 ? (0, 0, 0) : (255, 255, 255)
        default: // "pattern": smooth ramps with a hard square inside
            let inside = x >= width / 3 && x < width * 2 / 3 && y >= height / 3 && y < height * 2 / 3
            c = inside ? (240, 40, 40) : (UInt8(40 + x * 180 / max(1, width - 1)), UInt8(60 + y * 150 / max(1, height - 1)), 128)
        }
        rgba[i] = c.0; rgba[i + 1] = c.1; rgba[i + 2] = c.2; rgba[i + 3] = 255
        if y < bar || y >= height - bottom {
            rgba[i] = barValue; rgba[i + 1] = barValue; rgba[i + 2] = barValue; rgba[i + 3] = barAlpha
        }
    } }
    return rgba
}

func image(_ rgba: [UInt8], _ width: Int, _ height: Int) -> CGImage {
    let provider = CGDataProvider(data: Data(rgba) as CFData)!
    return CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: width * 4,
        space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.last.rawValue),
        provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent)!
}

func png(_ image: CGImage) -> Data {
    let output = NSMutableData()
    let destination = CGImageDestinationCreateWithData(output, "public.png" as CFString, 1, nil)!
    CGImageDestinationAddImage(destination, image, nil)
    guard CGImageDestinationFinalize(destination) else { fatalError("PNG encode") }
    return output as Data
}

// An image's pixels as straight sRGB RGBA8, top-left rows.
func pixels(_ image: CGImage) -> [UInt8] {
    let width = image.width, height = image.height
    let context = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8, bytesPerRow: width * 4,
        space: CGColorSpace(name: CGColorSpace.sRGB)!,
        bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue | CGBitmapInfo.byteOrder32Big.rawValue)!
    context.draw(image, in: CGRect(x: 0, y: 0, width: width, height: height))
    let data = context.data!.assumingMemoryBound(to: UInt8.self)
    var out = [UInt8](repeating: 0, count: width * height * 4)
    for k in 0..<(width * height) {
        let a = Int(data[k * 4 + 3])
        for c in 0..<3 { out[k * 4 + c] = a == 0 ? 0 : UInt8(min(255, (Int(data[k * 4 + c]) * 255 + a / 2) / a)) }
        out[k * 4 + 3] = UInt8(a)
    }
    return out
}

func row(_ rgba: [UInt8], width: Int, y: Int) -> [UInt8] { Array(rgba[(y * width * 4)..<((y + 1) * width * 4)]) }
func column(_ rgba: [UInt8], width: Int, height: Int, x: Int) -> [UInt8] {
    (0..<height).flatMap { y in rgba[((y * width + x) * 4)..<((y * width + x) * 4 + 4)] }
}
func grid(_ rgba: [UInt8], width: Int, height: Int, step: Int) -> [UInt8] {
    var out: [UInt8] = []
    for y in Swift.stride(from: 0, to: height, by: step) { for x in Swift.stride(from: 0, to: width, by: step) {
        out.append(contentsOf: rgba[((y * width + x) * 4)..<((y * width + x) * 4 + 4)])
    } }
    return out
}

struct Case { let name: String; let kind: String; let width: Int; let height: Int; let bar: Int; let barBottom: Int; let pixels: Int }

@main struct Export {
    static func main() throws {
        guard CommandLine.arguments.count == 2 else { fatalError("Pass a new output JSON") }
        let L = NowPlayingArtworkLoader.self
        var result: [String: Any] = [
            "fillLimit": Double(L.fillLimit), "maximumEnlargement": Double(L.maximumEnlargement),
            "backdropPixels": L.backdropPixels, "backdropBlur": Double(L.backdropBlur),
            "backdropExposure": Double(L.backdropExposure), "maximumEmbeddedBytes": L.maximumEmbeddedBytes,
            "maximumBytes": L.maximumBytes, "maximumPixelSize": L.maximumPixelSize,
        ]
        // layout(native:pixels:) rows.
        let sizes: [(Int, Int, Int)] = [
            (640, 640, 720), (600, 600, 720), (580, 580, 720), (576, 576, 720), (575, 575, 720), (512, 512, 720),
            (150, 150, 720), (150, 84, 720), (64, 64, 720), (1920, 200, 720), (1280, 720, 720), (96, 96, 720),
            (640, 640, 960), (1280, 720, 960), (150, 150, 1320), (0, 0, 720), (0, 150, 720), (2000, 3000, 720),
        ]
        result["layout"] = sizes.map { w, h, p -> [String: Any] in
            let plan = L.layout(native: CGSize(width: w, height: h), pixels: p)
            return ["width": w, "height": h, "pixels": p, "small": plan.small, "side": plan.side]
        }
        // trimmingLetterbox on a decoded picture: the rows it keeps.
        let trims: [(String, Int, Int, Int, Int, UInt8, UInt8)] = [
            ("bars14", 150, 112, 14, 14, 0, 255), ("bars13_15", 150, 112, 13, 15, 0, 255), ("bars14_8", 150, 112, 14, 8, 0, 255),
            ("bars1", 150, 112, 1, 1, 0, 255), ("alpha200", 150, 112, 14, 14, 0, 200), ("value30", 150, 112, 14, 14, 30, 255),
            ("value24", 150, 112, 14, 14, 24, 255), ("bars4", 150, 112, 4, 4, 0, 255), ("square", 150, 150, 14, 14, 0, 255),
            ("letterbox160", 160, 120, 15, 15, 0, 255), ("hqdefault", 480, 360, 45, 45, 0, 255),
        ]
        result["trim"] = trims.map { name, w, h, top, bottom, value, alpha -> [String: Any] in
            let source = image(picture("pattern", w, h, bar: top, barBottom: bottom, barValue: value, barAlpha: alpha), w, h)
            let trimmed = L.trimmingLetterbox(source)
            return ["name": name, "width": w, "height": h, "top": top, "bottom": bottom, "barValue": Int(value),
                    "barAlpha": Int(alpha), "keptWidth": trimmed.width, "keptHeight": trimmed.height]
        }
        // decode(_:pixelSize:) over encoded pictures.
        let cases: [Case] = [
            Case(name: "uniform128", kind: "uniform128", width: 150, height: 150, bar: 0, barBottom: 0, pixels: 720),
            Case(name: "uniform255", kind: "uniform255", width: 150, height: 150, bar: 0, barBottom: 0, pixels: 720),
            Case(name: "step96", kind: "step", width: 96, height: 96, bar: 0, barBottom: 0, pixels: 720),
            Case(name: "pattern150", kind: "pattern", width: 150, height: 150, bar: 0, barBottom: 0, pixels: 720),
            Case(name: "pattern150x84", kind: "pattern", width: 150, height: 84, bar: 0, barBottom: 0, pixels: 720),
            Case(name: "letterbox160", kind: "pattern", width: 160, height: 120, bar: 15, barBottom: 15, pixels: 720),
            Case(name: "hqdefault", kind: "pattern", width: 480, height: 360, bar: 45, barBottom: 45, pixels: 720),
            // Sizes whose region x factor is not whole (1.44): rounded up on
            // the height, rounded down, and rounded up on the width.
            Case(name: "pattern500x281", kind: "pattern", width: 500, height: 281, bar: 0, barBottom: 0, pixels: 720),
            Case(name: "pattern500x282", kind: "pattern", width: 500, height: 282, bar: 0, barBottom: 0, pixels: 720),
            Case(name: "pattern281x500", kind: "pattern", width: 281, height: 500, bar: 0, barBottom: 0, pixels: 720),
            Case(name: "wide1920x200", kind: "pattern", width: 1920, height: 200, bar: 0, barBottom: 0, pixels: 720),
            Case(name: "large640", kind: "pattern", width: 640, height: 640, bar: 0, barBottom: 0, pixels: 720),
        ]
        result["decode"] = cases.map { c -> [String: Any] in
            let data = png(image(picture(c.kind, c.width, c.height, bar: c.bar, barBottom: c.barBottom), c.width, c.height))
            guard let art = L.decode(data, pixelSize: c.pixels) else { fatalError("decode \(c.name)") }
            let fw = art.image.width, fh = art.image.height, fg = pixels(art.image)
            var out: [String: Any] = ["name": c.name, "kind": c.kind, "width": c.width, "height": c.height, "bar": c.bar,
                "pixels": c.pixels, "nativeWidth": Int(art.native.width), "nativeHeight": Int(art.native.height),
                "imageWidth": fw, "imageHeight": fh,
                "imageRow": hex(row(fg, width: fw, y: fh / 2)), "imageColumn": hex(column(fg, width: fw, height: fh, x: fw / 2))]
            if let backdrop = art.backdrop {
                let bw = backdrop.width, bh = backdrop.height, bg = pixels(backdrop)
                out["backdropWidth"] = bw; out["backdropHeight"] = bh
                out["backdropRow"] = hex(row(bg, width: bw, y: bh / 2))
                out["backdropGrid"] = hex(grid(bg, width: bw, height: bh, step: 5)); out["backdropGridStep"] = 5
            }
            return out
        }
        let json = try JSONSerialization.data(withJSONObject: result, options: [.prettyPrinted, .sortedKeys])
        try json.write(to: URL(fileURLWithPath: CommandLine.arguments[1]))
    }
}
