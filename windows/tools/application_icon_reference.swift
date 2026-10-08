import AppKit
import CryptoKit
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
        var rows: [[String: Any]] = []
        let offered = Set(HUDApplicationIcon.pickerCases.map(\.rawValue))
        for icon in HUDApplicationIcon.allCases {
            var row: [String: Any] = ["id": icon.rawValue, "title": icon.title, "offered": offered.contains(icon.rawValue)]
            for menu in [false, true] {
                let image = menu ? icon.menuBarImage() : icon.image(size: 64)
                guard let cg = image.cgImage(forProposedRect: nil, context: nil, hints: nil) else { fatalError("Missing original rendered icon") }
                let rep = NSBitmapImageRep(cgImage: cg)
                guard let png = rep.representation(using: .png, properties: [:]) else { fatalError("PNG encoding failed") }
                let name = icon.rawValue + (menu ? "-tray.png" : "-app.png")
                try png.write(to: out.appendingPathComponent(name), options: .withoutOverwriting)
                row[menu ? "tray" : "app"] = ["file": name, "width": cg.width, "height": cg.height, "sha256": SHA256.hash(data: png).map { String(format: "%02x", $0) }.joined(), "template": image.isTemplate]
            }
            rows.append(row)
        }
        var sourceAssets: [String: String] = [:]
        for name in HUDResources.used.sorted() {
            let bytes = try Data(contentsOf: HUDResources.root.appendingPathComponent(name))
            sourceAssets["Resources/" + name] = SHA256.hash(data: bytes).map { String(format: "%02x", $0) }.joined()
        }
        let result: [String: Any] = ["sourceCommit": "ca04f142185c7de40acd8523bdb563195d90a1d1", "sourceAssets": sourceAssets, "icons": rows, "method": "Unchanged HUDApplicationIcon/EndfieldGameIcon rendering; detached resource resolver and English title lookup; no window or application activation"]
        try JSONSerialization.data(withJSONObject: result, options: [.sortedKeys, .prettyPrinted]).write(to: out.appendingPathComponent("manifest.json"), options: .withoutOverwriting)
    }
}
