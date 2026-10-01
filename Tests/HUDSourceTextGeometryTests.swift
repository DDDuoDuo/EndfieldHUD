import Foundation
import simd

/// Original font metrics and known local geometry; no window or game process.
enum HUDSourceTextGeometryTests {
    static func run() -> Int {
        var count = 0
        func check(_ value: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1
            if !value { fatalError(message, file: file, line: line) }
        }
        func close(_ value: Double, _ expected: Double, _ message: String, tolerance: Double = 1e-6,
                   file: StaticString = #file, line: UInt = #line) {
            check(value.isFinite && abs(value - expected) <= tolerance, message, file: file, line: line)
        }
        func fails(_ message: String, _ operation: () throws -> Void) {
            do { try operation(); check(false, message) } catch { check(true, message) }
        }
        do {
            let document = try HUDSourceWatchDocument()
            let generator = try HUDSourceTextGeometry(document: document)
            let resolved = try document.scene.resolve()
            guard let label = document.buttons.compactMap({ $0.label }).first(where: { $0.literal == "干员" }) else {
                fatalError("Missing original Watch operator label")
            }
            let rect = HUDSourceRect(origin: SIMD2(-90, -25), size: SIMD2(180, 50))
            let mesh = try generator.build(on: label.nodeID, rect: rect, sdfScale: 0.0075)
            check(mesh.positions.count == 8 && mesh.indices.count == 12, "Two source glyphs produce two quads")
            close(mesh.pointSize, 26, "Operator label uses its original 26-point maximum")
            close(mesh.advance, 52, "Two full-em Chinese advances are 26 + 26")
            close(mesh.baseline, -8.976190476190476, "Middle alignment uses face ascent 39 and descent -10 at 26/42 scale")
            // The source 干 glyph has bearing (2.359375,32.84375),
            // width37.203125/height35.609375. Its base material has padding1.25.
            close(Double(mesh.positions[0].x), -25.313244047619047, "Glyph bearing and material padding set BL X", tolerance: 3e-6)
            close(Double(mesh.positions[0].y), -11.462053571428571, "Glyph height and baseline set BL Y", tolerance: 3e-6)
            close(Double(mesh.positions[1].y), 12.129464285714286, "Source bearing sets glyph top", tolerance: 3e-6)
            close(Double(mesh.uv[0].x), 0.7132568359375, "Source atlas X1462 minus padding divided by2048")
            close(Double(mesh.uv[0].y), 0.72845458984375, "Source atlas Y2985 minus padding divided by4096")
            close(Double(mesh.uv[2].x), 0.7330322265625, "UV uses integer atlas width38 independently of glyph metric width")
            close(Double(mesh.uv[2].y), 0.73785400390625, "UV preserves original bottom-left atlas origin")
            check(mesh.uv2.prefix(4).map { $0.x } == [0, 511, 2_093_567, 2_093_056], "Packed UV2 follows the source shader's 4096 stride")
            close(Double(mesh.uv2[0].y), 26.0 / 42.0 * 0.0075, "SDF scale includes injected world/canvas scale", tolerance: 1e-8)
            check(mesh.normals.allSatisfy { $0 == SIMD3<Float>(0, 0, -1) }, "TMP normal points along local negative Z")
            let a = mesh.positions[0], b = mesh.positions[1], c = mesh.positions[2]
            let ab = SIMD3<Float>(b.x - a.x, b.y - a.y, b.z - a.z)
            let ac = SIMD3<Float>(c.x - a.x, c.y - a.y, c.z - a.z)
            check(simd_cross(ab, ac).z < 0, "Source triangle winding agrees with TMP normal")
            check(mesh.atlasID.rawValue == "CAB-e1b9246317f3a094cb651bec654f7b76:8016279098669342999", "Original 64-bit atlas ID remains exact")
            close(Double(mesh.color.x), 50.0 / 255.0, "TMP color32 quantization preserves source dark text")
            let doubledScale = try generator.build(on: label.nodeID, rect: rect, sdfScale: 0.015)
            check(doubledScale.positions == mesh.positions && doubledScale.uv == mesh.uv,
                  "Animated world scale affects SDF scale without baking world geometry into local vertices")
            close(Double(doubledScale.uv2[0].y), Double(mesh.uv2[0].y) * 2, "SDF scale retargets with the source transform", tolerance: 1e-8)
            let narrow = try generator.build(on: label.nodeID, rect: HUDSourceRect(origin: SIMD2(-20, -25), size: SIMD2(40, 50)), sdfScale: 1)
            close(narrow.pointSize, 20, "One-line autosizing fits two em advances into40 at20 points")
            fails("Glyph wrapping below original minimum point size is explicit") {
                _ = try generator.build(on: label.nodeID, rect: HUDSourceRect(origin: .zero, size: SIMD2(20, 50)), sdfScale: 1)
            }
            fails("Multiline text cannot silently render as one line") {
                _ = try generator.build(on: label.nodeID, rect: rect, sdfScale: 1, literal: "干员\n活动")
            }
            fails("A missing glyph cannot silently switch to a system font") {
                _ = try generator.build(on: label.nodeID, rect: rect, sdfScale: 1, literal: "🦊")
            }
            var builtLabels = 0
            for node in document.scene.nodes {
                guard document.component("UIText", on: node.id) != nil,
                      let literal = generator.literal(on: node.id), !literal.isEmpty, literal != "NEW",
                      let localRect = resolved[node.id]?.rect else { continue }
                let text = try generator.build(on: node.id, rect: localRect, sdfScale: 1)
                check(text.positions.count == literal.unicodeScalars.count * 4, "Every original fixed label has source glyphs: \(node.path)")
                check(text.positions.allSatisfy { $0.x.isFinite && $0.y.isFinite && $0.z == 0 && $0.w == 1 },
                      "Source text geometry stays finite and local: \(node.path)")
                if literal == "贵重品库" || literal == "问卷中心" { close(text.pointSize, 25, "Four-em label fits source width100 at25") }
                if literal == "寻访" || literal == "工业模式" { close(text.pointSize, 25.7, "Source height30 limits49/42 em box to25.7") }
                if literal == "Baker" { close(text.advance, 70.09747023809524, "Baker uses original Latin glyph advances") }
                builtLabels += 1
            }
            check(builtLabels == 28, "All28 serialized non-NEW fixed Watch text nodes build, including shadow twins")
        } catch { fatalError("Source text geometry fixture failed: \(error)") }
        return count
    }
}
