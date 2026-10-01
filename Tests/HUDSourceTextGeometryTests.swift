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
            let narrow = try generator.build(on: label.nodeID, rect: HUDSourceRect(origin: SIMD2(-24, -25), size: SIMD2(48, 50)), sdfScale: 1)
            close(narrow.pointSize, 24, "One-line autosizing fits two em advances into48 within the authored minimum22")
            fails("Source minimum22 cannot be lowered to20 to avoid actual character wrapping") {
                _ = try generator.build(on: label.nodeID, rect: HUDSourceRect(origin: .zero, size: SIMD2(40, 50)), sdfScale: 1)
            }
            fails("Glyph wrapping below original minimum point size is explicit") {
                _ = try generator.build(on: label.nodeID, rect: HUDSourceRect(origin: .zero, size: SIMD2(20, 50)), sdfScale: 1)
            }
            fails("Multiline text cannot silently render as one line") {
                _ = try generator.build(on: label.nodeID, rect: rect, sdfScale: 1, literal: "干员\n活动")
            }
            fails("A missing glyph cannot silently switch to a system font") {
                _ = try generator.build(on: label.nodeID, rect: rect, sdfScale: 1, literal: "🦊")
            }
            close(try generator.preferredSize(on: label.nodeID).x, 52.01,
                  "Preferred width uses authored maximum26 and TMP hundredth rounding")
            guard let original = document.component("UIText", on: label.nodeID) else { fatalError("Missing source text component") }
            func changedGenerator(overflow: Int) throws -> HUDSourceTextGeometry {
                var fields = original.data
                fields["m_enableAutoSizing"] = .bool(false)
                fields["m_fontSize"] = .number(26)
                fields["m_enableWordWrapping"] = .bool(false)
                fields["m_overflowMode"] = .number(Double(overflow))
                let changed = HUDSourceWatchComponent(id: original.id, type: original.type, script: original.script, data: fields)
                return try HUDSourceTextGeometry(document: document, additionalComponents: [label.nodeID: [changed]])
            }
            let tooSmall = HUDSourceRect(origin: .zero, size: SIMD2(10, 5))
            let overflowing = try changedGenerator(overflow: 0).build(on: label.nodeID, rect: tooSmall, sdfScale: 1)
            check(overflowing.positions.count == 8 && overflowing.clipRect == nil,
                  "Source no-wrap Overflow preserves glyphs beyond both rectangle bounds")
            close(overflowing.pointSize, 26, "Overflow does not invent a lower fixed point size")
            let masked = try changedGenerator(overflow: 2).build(on: label.nodeID, rect: tooSmall, sdfScale: 1)
            check(masked.positions == overflowing.positions && masked.clipRect?.size == tooSmall.size,
                  "Masking retains glyph vertices and declares the renderer's local clip")
            fails("Ellipsis cannot silently use Overflow behavior") {
                _ = try changedGenerator(overflow: 1).build(on: label.nodeID, rect: tooSmall, sdfScale: 1)
            }
            var builtNew = 0
            for node in document.scene.nodes {
                guard generator.literal(on: node.id) == "NEW", let localRect = resolved[node.id]?.rect else { continue }
                let preferred = try generator.preferredSize(on: node.id)
                let filled = HUDSourceRect(origin: localRect.origin,
                    size: SIMD2(max(localRect.size.x, preferred.x), max(localRect.size.y, preferred.y)))
                let bold = try generator.build(on: node.id, rect: filled, sdfScale: 1)
                check(bold.positions.count == 12 && bold.uv2.allSatisfy { $0.y < 0 },
                      "Source NEW uses three source glyphs and negative SDF bold scale: \(node.path)")
                check(bold.pointSize.isFinite && preferred.x > 0 && preferred.y > 0,
                      "Original NEW can supply ContentSizeFitter metrics even at serialized width zero")
                builtNew += 1
            }
            check(builtNew == 28, "All28 original NEW text components have the same supported source bold path")
            var builtLabels = 0
            let fixedLabels = Set(document.labels["nodes"].array.compactMap { value -> HUDSourceID? in
                guard let text = value["cn_literal"].string, !text.isEmpty, text != "NEW",
                      let node = value["node_id"].string else { return nil }
                return HUDSourceID(rawValue: node)
            })
            for node in document.scene.nodes {
                guard fixedLabels.contains(node.id), document.component("UIText", on: node.id) != nil,
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
            var auditBuilt = 0, auditMissing = 0, auditErrors: [[String: String]] = []
            for node in document.scene.nodes {
                guard document.component("UIText", on: node.id) != nil else { continue }
                guard let value = generator.literal(on: node.id), !value.isEmpty,
                      let bounds = resolved[node.id]?.rect else { auditMissing += 1; continue }
                do {
                    let preferred = try generator.preferredSize(on: node.id)
                    let size = SIMD2(bounds.size.x > 0 ? bounds.size.x : preferred.x,
                                     bounds.size.y > 0 ? bounds.size.y : preferred.y)
                    _ = try generator.build(on: node.id, rect: HUDSourceRect(origin: bounds.origin, size: size), sdfScale: 1)
                    auditBuilt += 1
                } catch {
                    auditErrors.append(["path": node.path, "serializedText": value, "error": String(describing: error)])
                }
            }
            check(auditBuilt == 60 && auditMissing == 15 && auditErrors.count == 2,
                  "77-node audit accounts for60 single-line source values,15 runtime inputs and2 serialized multiline placeholders")
            check(auditErrors.allSatisfy { $0["serializedText"]?.hasSuffix("\n") == true },
                  "Only the two serialized Top counters' newline placeholders require the unsupported multiline path")
            let audit: [String: Any] = ["sourceTextNodes": auditBuilt + auditMissing + auditErrors.count,
                "singleLineBuilt": auditBuilt, "unresolvedRuntimeInputs": auditMissing, "explicitUnsupported": auditErrors]
            let auditData = try JSONSerialization.data(withJSONObject: audit, options: [.sortedKeys])
            print("Source text audit: " + String(decoding: auditData, as: UTF8.self))
            let domain = try HUDSourceWatchDomain()
            let domainGenerator = try HUDSourceTextGeometry(document: document,
                additionalComponents: domain.components, additionalLabels: domain.labels,
                additionalMaterials: domain.materials)
            let domainResolved = try domain.scene.resolve()
            var builtDomain = 0
            for (node, components) in domain.components {
                guard components.contains(where: { $0.kind == "UIText" }),
                      let text = domainGenerator.literal(on: node), !text.isEmpty,
                      let bounds = domainResolved[node]?.rect else { continue }
                let geometry = try domainGenerator.build(on: node, rect: bounds, sdfScale: 1)
                check(geometry.positions.count == text.unicodeScalars.count * 4,
                      "Domain runtime CN localization uses the common original glyph geometry")
                builtDomain += 1
            }
            check(builtDomain == 6, "Six loaded Region01 map labels use table-derived localized runtime names")
        } catch { fatalError("Source text geometry fixture failed: \(error)") }
        return count
    }
}
