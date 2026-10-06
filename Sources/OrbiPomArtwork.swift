import AppKit

/// Fifteen small original PNGs, decoded only when the module uses them. Images
/// share this bounded cache; the large source archive never ships with the app.
enum OrbiPomArtwork {
    private static var images: [String: NSImage] = [:]
    static func image(level: Int) -> NSImage? {
        guard (1...11).contains(level) else { return nil }
        return image(named: "level-\(level)")
    }
    static func skill(_ skill: OrbiPomSkill) -> NSImage? { image(named: "skill-" + skill.rawValue) }
    static func releaseDecodedImages() { images.removeAll(keepingCapacity: false) }
    private static func image(named name: String) -> NSImage? {
        if let cached = images[name] { return cached }
        guard let url = HUDResources.url(for: "OrbiPom/" + name + ".png"), let result = NSImage(contentsOf: url) else { return nil }
        images[name] = result; return result
    }
}
