import Foundation

/// Standalone: compiled with and without HUD_RELEASE by test-release-resources.sh.
@main struct HUDResourcesReleaseProbe {
    static func main() throws {
        let fileManager = FileManager.default
        let temporary = fileManager.temporaryDirectory.appendingPathComponent("EndfieldHUD-resource-probe-" + UUID().uuidString)
        defer { try? fileManager.removeItem(at: temporary) }
        let bundleURL = temporary.appendingPathComponent("Fixture.bundle")
        let resources = bundleURL.appendingPathComponent("Contents/Resources")
        try fileManager.createDirectory(at: resources.appendingPathComponent("WorldMap"), withIntermediateDirectories: true)
        let plist: [String: Any] = ["CFBundleIdentifier": "io.github.endfieldhud.resource-probe", "CFBundlePackageType": "BNDL"]
        try PropertyListSerialization.data(fromPropertyList: plist, format: .xml, options: 0)
            .write(to: bundleURL.appendingPathComponent("Contents/Info.plist"))
        let expected = resources.appendingPathComponent("WorldMap/Terrain.bin")
        try Data("bundled-fixture".utf8).write(to: expected)
        guard let bundle = Bundle(url: bundleURL),
              HUDResources.url(for: "WorldMap/Terrain.bin", bundle: bundle)?.standardizedFileURL == expected.standardizedFileURL else {
            throw ProbeError.failed("Bundle resources were not preferred")
        }
        guard HUDResources.url(for: "not-a-resource", bundle: bundle) == nil,
              HUDResources.url(for: "../Info.plist", bundle: bundle) == nil else {
            throw ProbeError.failed("Missing or non-relative resources must not resolve")
        }
        #if HUD_RELEASE
        guard HUDResources.url(for: "WorldMap/Countries.bin", bundle: bundle) == nil else {
            throw ProbeError.failed("Release build fell back to source assets outside the bundle")
        }
        print("PASS: release bundle lookup and no source fallback")
        #else
        guard HUDResources.url(for: "WorldMap/Countries.bin", bundle: bundle) != nil else {
            throw ProbeError.failed("Development lookup lost its checkout resource fallback")
        }
        print("PASS: development bundle lookup and source fallback")
        #endif
    }
    private enum ProbeError: Error { case failed(String) }
}
