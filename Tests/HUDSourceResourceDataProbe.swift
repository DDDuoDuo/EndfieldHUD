import Foundation
import CryptoKit

/// Independent native decoder parity against every actual packaged resource.
/// No application launch, user stores, desktop capture or permissions involved.
@main struct HUDSourceResourceDataProbe {
    static func main() throws {
        guard CommandLine.arguments.count == 3 else { fatalError("SOURCE_ROOT PACKED_ROOT") }
        let source = URL(fileURLWithPath: CommandLine.arguments[1])
        let packed = URL(fileURLWithPath: CommandLine.arguments[2])
        let inventory = try JSONSerialization.jsonObject(with: Data(contentsOf: packed.appendingPathComponent("runtime-inventory.json"))) as! [String: Any]
        let records = inventory["files"] as! [[String: Any]]
        var compressedCount = 0
        for record in records {
            let name = record["path"] as! String
            let original = try Data(contentsOf: source.appendingPathComponent(name))
            let result = try HUDSourceResourceData.read(packed.appendingPathComponent(name))
            guard original == result else { fatalError("Native decoder changed source bytes: \(name)") }
            let hash = SHA256.hash(data: result).map { String(format: "%02x", $0) }.joined()
            guard hash == record["source_sha256"] as? String else { fatalError("Inventory source digest differs: \(name)") }
            if record["encoding"] as? String == "raw-deflate-v1" { compressedCount += 1 }
        }
        let compact = try JSONSerialization.jsonObject(with: HUDSourceResourceData.read(packed.appendingPathComponent("runtime-materials.json"))) as! [String: Any]
        let allMaterials = try JSONSerialization.jsonObject(with: Data(contentsOf: source.appendingPathComponent("materials.json"))) as! [Any]
        let selectedMaterials = (compact["source_indices"] as! [Int]).map { allMaterials[$0] }
        precondition(NSArray(array: selectedMaterials).isEqual(to: compact["materials"] as! [Any]),
                     "Native compact material metadata changed source values")
        func rejects(_ data: Data) {
            do { _ = try HUDSourceResourceData.decode(data); fatalError("Invalid container was accepted") }
            catch { }
        }
        let magic = Data([69, 72, 85, 68, 90, 48, 49, 0])
        rejects(magic)
        rejects(magic + Data(repeating: 255, count: 8))
        rejects(magic + Data(repeating: 0, count: 8))
        let sample = records.first { $0["encoding"] as? String == "raw-deflate-v1" }!["path"] as! String
        var corrupted = try Data(contentsOf: packed.appendingPathComponent(sample))
        corrupted[8] ^= 1
        rejects(corrupted)
        let blue = SIMD3<Float>(0.02, 0.12, 0.9)
        let ring = SIMD4<Float>(1, 0.33, 0, 0.78431374)
        precondition(HUDSourceDesktopAccent.replacingYellow(ring, accent: nil) == ring)
        precondition(HUDSourceDesktopAccent.replacingYellow(ring, accent: blue) == SIMD4(blue.x, blue.y, blue.z, ring.w))
        let hdr = HUDSourceDesktopAccent.replacingYellow(SIMD4(4, 3.2, 0.2, 0.25), accent: blue)
        precondition(hdr == SIMD4(blue.x * 4, blue.y * 4, blue.z * 4, 0.25))
        for neutral in [SIMD4<Float>(1, 1, 1, 0.4), SIMD4<Float>(0.2, 0.2, 0.2, 1),
                        SIMD4<Float>(0, 0, 0, 1), SIMD4<Float>(1, 0.1, 0, 1)] {
            precondition(HUDSourceDesktopAccent.replacingYellow(neutral, accent: blue) == neutral)
        }
        precondition(HUDSourceDesktopAccent.materialValue([1, 1, 0, 0], isColor: false, accent: blue) == [1, 1, 0, 0])
        precondition(HUDSourceDesktopAccent.materialValue([1, 1, 0, 0.6], isColor: true, accent: blue) == [blue.x, blue.y, blue.z, 0.6])
        print("PASS: desktop accent changes ring/color channels, preserves alpha/HDR, neutral colors, vectors and reference mode")
        print("PASS: \(records.count) native/source byte comparisons, \(compressedCount) compressed payloads and malformed container rejection")
    }
}
