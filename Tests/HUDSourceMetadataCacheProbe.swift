import Foundation

/// CPU-only regression probe. It never creates an application, view, Metal
/// device, texture or window, and reads the supplied app only as a directory.
@main
struct HUDSourceMetadataCacheProbe {
    enum Failure: Error, CustomStringConvertible {
        case invalid(String)
        var description: String { switch self { case .invalid(let message): return message } }
    }

    static func main() throws {
        guard CommandLine.arguments.count == 3 else {
            throw Failure.invalid("Usage: HUDSourceMetadataCacheProbe <WatchSource directory> <new scratch directory>")
        }
        let source = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
        let root = URL(fileURLWithPath: CommandLine.arguments[2], isDirectory: true)
        let manager = FileManager.default
        guard !manager.fileExists(atPath: root.path) else {
            throw Failure.invalid("Metadata probe requires a new, owned scratch directory")
        }
        try manager.createDirectory(at: root.appendingPathComponent("Meshes"), withIntermediateDirectories: true)

        // Copy just the selected CPU metadata. No textures, shader programs,
        // source scenes or app executable are needed by this probe.
        let shaders = try manager.contentsOfDirectory(atPath: source.path).filter { $0.hasSuffix("-shader.json") }
        let names = shaders + ["runtime-inventory.json", "runtime-selection.json", "runtime-materials.json",
            "render-color-policy.json", "textures.json"]
            + ["Equipring", "watchline", "Plane", "Cylinder"].map { "Meshes/" + $0 + ".json" }
        for name in names {
            try manager.copyItem(at: source.appendingPathComponent(name), to: root.appendingPathComponent(name))
        }

        var assertions = 0
        func check(_ condition: Bool, _ message: String) throws {
            guard condition else { throw Failure.invalid(message) }
            assertions += 1
        }
        let first = try HUDSourceMetalRenderer.metadataIdentityForVerification(root: root)
        let repeated = try HUDSourceMetalRenderer.metadataIdentityForVerification(root: root)
        try check(first == repeated, "Unchanged resource metadata must reuse one catalog")
        let values = try HUDSourceMetalRenderer.verifyMetadataValuesForVerification(root: root)
        try check(values["catalogs"] == 1 && values["objects"] == 7 && values["shaders"] == 65,
                  "The selected metadata catalog shape changed")
        // This is the reviewed desktop material closure. The verification API
        // compares every cached numeric input/type/texture with a fresh parse.
        try check(values["materials"] == 132, "The reviewed 132-material desktop closure changed")
        try check((values["sourceBytes"] ?? Int.max) <= 16 * 1024 * 1024,
                  "The CPU metadata source-byte cap must remain bounded")

        let inventory = root.appendingPathComponent("runtime-inventory.json")
        var data = try Data(contentsOf: inventory)
        data.append(10) // Valid JSON whitespace still changes inventory identity.
        try data.write(to: inventory)
        let replaced = try HUDSourceMetalRenderer.metadataIdentityForVerification(root: root)
        try check(replaced != first, "A changed inventory must replace the cached catalog")

        let shader = root.appendingPathComponent("fx-shader.json")
        let original = try Data(contentsOf: shader)
        try Data("{".utf8).write(to: shader)
        var rejected = false
        do { _ = try HUDSourceMetalRenderer.metadataIdentityForVerification(root: root) }
        catch { rejected = true }
        try check(rejected, "A corrupt metadata file must reject a stale cache hit")
        try original.write(to: shader)
        let recovered = try HUDSourceMetalRenderer.verifyMetadataValuesForVerification(root: root)
        try check(recovered == values, "A failed load must allow an exact subsequent recovery")

        try HUDSourceMetalRenderer.prepareDesktopMetadataIfNeeded(resourceRoot: root)
        let prepared = try HUDSourceMetalRenderer.metadataIdentityForVerification(root: root)
        let ready = try HUDSourceMetalRenderer.metadataIdentityForVerification(root: root)
        try check(prepared == ready, "Synchronous readiness must reuse the current catalog")
        let reference = root.appendingPathComponent("reference-without-selection", isDirectory: true)
        try manager.createDirectory(at: reference, withIntermediateDirectories: false)
        try HUDSourceMetalRenderer.prepareDesktopMetadataIfNeeded(resourceRoot: reference)
        try HUDSourceMetalRenderer.prepareDesktopMetadataIfNeeded(resourceRoot: root.appendingPathComponent("missing"))
        let afterNoOp = try HUDSourceMetalRenderer.metadataIdentityForVerification(root: root)
        try check(afterNoOp == ready, "Reference and missing resource roots must leave the desktop cache intact")
        print("PASS: \(assertions) CPU metadata cache checks; 132 material inputs equal fresh parsing; no GPU or window")
    }
}
