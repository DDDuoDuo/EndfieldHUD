import Foundation

/// Release apps use only their signed bundle. Command-line development tools
/// may read the source assets without assembling a temporary application.
enum HUDResources {
    static func url(for relativePath: String, bundle: Bundle = .main) -> URL? {
        guard !relativePath.isEmpty, !relativePath.hasPrefix("/"),
              !relativePath.split(separator: "/").contains("..") else { return nil }
        if let bundled = bundle.resourceURL?.appendingPathComponent(relativePath),
           FileManager.default.fileExists(atPath: bundled.path) { return bundled }
        #if !HUD_RELEASE
        let source = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
            .deletingLastPathComponent().appendingPathComponent("Resources")
            .appendingPathComponent(relativePath)
        if FileManager.default.fileExists(atPath: source.path) { return source }
        #endif
        return nil
    }
}
