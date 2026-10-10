import AppKit
import CryptoKit
// Renders the executable icon with the unchanged HUDApplicationIcon /
// EndfieldGameIcon code (the same path NSApp.applicationIconImage uses) at
// every Windows icon size. No window, activation or preference access.
enum L10n { static func text(_ en: String, _ zh: String) -> String { en } }
enum HUDResources {
    static var root = URL(fileURLWithPath: "/")
    static var used: Set<String> = []
    static func url(for name: String) -> URL? {
        let url = root.appendingPathComponent(name)
        guard FileManager.default.fileExists(atPath: url.path) else { return nil }
        used.insert(name); return url
    }
}
@main struct Export {
    static func main() throws {
        guard CommandLine.arguments.count == 3 else { fatalError("Pass source Resources and a new output directory") }
        HUDResources.root = URL(fileURLWithPath: CommandLine.arguments[1])
        let out = URL(fileURLWithPath: CommandLine.arguments[2])
        guard !FileManager.default.fileExists(atPath: out.path) else { fatalError("Output must be new") }
        try FileManager.default.createDirectory(at: out, withIntermediateDirectories: false)
        var sizes: [[String: Any]] = []
        for size in [16, 20, 24, 32, 40, 48, 64, 96, 128, 256] {
            let image = HUDApplicationIcon.endfield.image(size: CGFloat(size))
            // Exact pixel grid: rasterize the NSImage at size x size pixels.
            guard let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: size, pixelsHigh: size, bitsPerSample: 8, samplesPerPixel: 4,
                                             hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0) else { fatalError("Bitmap") }
            rep.size = NSSize(width: size, height: size)
            NSGraphicsContext.saveGraphicsState()
            NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
            NSGraphicsContext.current?.imageInterpolation = .high
            image.draw(in: NSRect(x: 0, y: 0, width: size, height: size), from: .zero, operation: .copy, fraction: 1)
            NSGraphicsContext.restoreGraphicsState()
            guard let png = rep.representation(using: .png, properties: [:]) else { fatalError("PNG encoding failed") }
            let name = "endfield-\(size).png"
            try png.write(to: out.appendingPathComponent(name), options: .withoutOverwriting)
            sizes.append(["size": size, "file": name, "sha256": SHA256.hash(data: png).map { String(format: "%02x", $0) }.joined()])
        }
        var sourceAssets: [String: String] = [:]
        for name in HUDResources.used.sorted() {
            let bytes = try Data(contentsOf: HUDResources.root.appendingPathComponent(name))
            sourceAssets["Resources/" + name] = SHA256.hash(data: bytes).map { String(format: "%02x", $0) }.joined()
        }
        let result: [String: Any] = ["sourceCommit": "ca04f142185c7de40acd8523bdb563195d90a1d1", "icon": "endfield", "sourceAssets": sourceAssets, "sizes": sizes,
                                     "method": "Unchanged HUDApplicationIcon.image(size:) for .endfield at each Windows icon size; detached resource resolver; no window"]
        try JSONSerialization.data(withJSONObject: result, options: [.sortedKeys, .prettyPrinted]).write(to: out.appendingPathComponent("provenance.json"), options: .withoutOverwriting)
    }
}
