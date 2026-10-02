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
            // Keep the frozen 28-label WatchPanel_PC baseline independent of
            // the separately validated runtime-loaded business-card subtree.
            let document = try HUDSourceWatchDocument(includeWidgets: false)
            count += try verifySinglePassDocument(document)
            count += try verifyDesktopDocumentCache(root: document.root)
            count += try verifyDesktopProfileSubset(root: document.root)
            let generator = try HUDSourceTextGeometry(document: document)
            let resolved = try document.scene.resolve()
            count += try verifyDesktopLayoutWithoutSourceText(document: document, text: generator)
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

    /// Compare the combined loader with the independent original decoders,
    /// including every source curve key and opaque component payload.
    private static func verifySinglePassDocument(_ document: HUDSourceWatchDocument) throws -> Int {
        struct Details: Decodable {
            let nodes: [HUDSourceWatchDocument.NodeComponents]
            let buttons: [HUDSourceWatchButton]
            enum CodingKeys: String, CodingKey { case nodes, buttons = "main_buttons" }
        }
        struct Extra: Decodable {
            let animators: [HUDSourceWatchDocument.Animator]
            enum CodingKeys: String, CodingKey { case animators = "controller_instances" }
        }
        func same(_ a: HUDSourceJSONValue, _ b: HUDSourceJSONValue) -> Bool {
            switch (a, b) {
            case (.null, .null): return true
            case let (.string(x), .string(y)): return x == y
            case let (.number(x), .number(y)): return x == y || (x.isNaN && y.isNaN)
            case let (.bool(x), .bool(y)): return x == y
            case let (.array(x), .array(y)): return x.count == y.count && zip(x, y).allSatisfy { same($0, $1) }
            case let (.object(x), .object(y)): return x.count == y.count && x.allSatisfy { key, value in y[key].map { same(value, $0) } ?? false }
            default: return false
            }
        }
        let decoder = HUDSourceJSON.decoder(), encoder = HUDSourceJSON.encoder()
        encoder.outputFormatting = [.sortedKeys]
        let sceneData = try HUDSourceResourceData.read(document.root.appendingPathComponent("scene.json"))
        let clipData = try HUDSourceResourceData.read(document.root.appendingPathComponent("clips.json"))
        let independentScene = try decoder.decode(HUDSourceScene.self, from: sceneData)
        let independentClips = try decoder.decode(HUDSourceAnimationLibrary.self, from: clipData)
        let combinedSceneData = try encoder.encode(document.scene), originalSceneData = try encoder.encode(independentScene)
        let combinedClipData = try encoder.encode(document.library), originalClipData = try encoder.encode(independentClips)
        precondition(combinedSceneData == originalSceneData, "Single-pass scene decoding must preserve all original fields")
        precondition(combinedClipData == originalClipData, "Single-pass animation decoding must preserve every authored key exactly")
        let details = try decoder.decode(Details.self, from: sceneData), extra = try decoder.decode(Extra.self, from: clipData)
        precondition(details.nodes.count == document.components.count && details.nodes.allSatisfy { record in
            guard let actual = document.components[record.id], actual.count == record.components.count else { return false }
            return zip(record.components, actual).allSatisfy { before, after in
                before.id == after.id && before.type == after.type && before.script == after.script
                    && same(.object(before.data), .object(after.data))
            }
        }, "Combined decoding must preserve component IDs, enabled flags and unknown payload fields")
        precondition(zip(details.buttons, document.buttons).allSatisfy { before, after in
            before.nodeID == after.nodeID && before.path == after.path && before.labels.count == after.labels.count
                && zip(before.labels, after.labels).allSatisfy { $0.nodeID == $1.nodeID && $0.textID == $1.textID && $0.literal == $1.literal }
        } && details.buttons.count == document.buttons.count, "Combined decoding must preserve button and label bindings")
        precondition(zip(extra.animators, document.animators).allSatisfy { before, after in
            before.rootID == after.rootID && before.controllerName == after.controllerName && before.states.count == after.states.count
                && zip(before.states, after.states).allSatisfy { $0.name == $1.name && $0.clipID == $1.clipID }
        } && extra.animators.count == document.animators.count, "Combined decoding must preserve controller-state bindings")
        let temporary = FileManager.default.temporaryDirectory.appendingPathComponent("endfield-document-" + UUID().uuidString)
        try FileManager.default.createDirectory(at: temporary, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: temporary) }
        var invalid = try JSONSerialization.jsonObject(with: originalSceneData) as! [String: Any]
        var root = (invalid["nodes"] as! [[String: Any]]).first { ($0["id"] as? String) == document.scene.rootID.rawValue }!
        root["components"] = [] as [Any]
        invalid["nodes"] = [root, root]; invalid["main_buttons"] = [] as [Any]
        try JSONSerialization.data(withJSONObject: invalid).write(to: temporary.appendingPathComponent("scene.json"))
        try Data("{\"clips\":[],\"controller_instances\":[]}".utf8).write(to: temporary.appendingPathComponent("clips.json"))
        do {
            _ = try HUDSourceWatchDocument(resourceRoot: temporary, includeWidgets: false, includeSourceText: false)
            fatalError("Combined document decoding must reject duplicate graph IDs")
        } catch HUDSourceError.invalid(let reason) {
            precondition(reason.hasPrefix("Duplicate node "), "Combined document must retain strict graph validation")
        }
        return 6
    }

    private static func verifyDesktopProfileSubset(root: URL) throws -> Int {
        let desktop = try HUDSourceWatchDocument(resourceRoot: root, includeWidgets: false, includeSourceText: false, includeDesktopProfile: true)
        let original = try HUDSourceDesktopProfileCard(data: Data(contentsOf: root.appendingPathComponent("Widgets/widget.json")))
        guard let card = desktop.desktopProfileCard else { fatalError("Desktop identity uses the source prefab") }
        var count = 0
        func check(_ value: Bool, _ message: String) { count += 1; precondition(value, message) }
        check(desktop.widgets == nil && desktop.fonts.object.isEmpty && desktop.labels.object.isEmpty,
              "The selected card does not load mutable banner playback, game fonts or labels")
        check(card.scene.nodes.count == 45 && card.sprites["sprites"].array.count == 21,
              "Desktop mounts only the selected profile subtree and its sprite closure")
        func sameTransform(_ a: HUDSourceTransform, _ b: HUDSourceTransform) -> Bool {
            a.kind == b.kind && a.localPosition == b.localPosition && a.localRotation == b.localRotation
                && a.localScale == b.localScale && a.rect == b.rect
        }
        for node in card.scene.nodes {
            guard let before = original.scene.node(node.id), let mounted = desktop.scene.node(node.id) else { fatalError("Lost authored profile node") }
            check(sameTransform(before.transform, node.transform) && before.active == node.active && before.name == node.name,
                  "Every selected node retains the authored source transform, name and activation")
            check(sameTransform(mounted.transform, before.transform) && mounted.path.hasSuffix("/" + before.path),
                  "Mounting keeps exact card geometry under the source opening/closing parent")
            check(node.childIDs.allSatisfy { card.scene.node($0) != nil }, "The selected card hierarchy is closed")
        }
        check(card.buttonIDs.count == 4 && card.buttonIDs.allSatisfy { desktop.component("UIButton", on: $0) != nil },
              "All authored identity targets keep their source UIButton hit contract")
        let forbidden = ["Banner", "BirthdayTipsNode", "NameListNode", "AdventureRewardEntry", "RedDot"]
        check(!card.scene.nodes.contains { node in forbidden.contains { node.path.contains($0) } },
              "Game account, reward, notification and banner content stays excluded")
        let textures = Set(card.sprites["source_textures"].array.compactMap { $0["id"].string })
        check(card.defaultBackgroundSprite?["name"].string == "business_card_topic_normal_1"
              && card.defaultBackgroundSprite?["texture"]["id"].string.map(textures.contains) == true,
              "The new desktop default is an authored dark card already in the selected texture closure")
        check(card.artworkGlowNodeIDs.count == 2 && card.artworkGlowNodeIDs.allSatisfy {
            desktop.component("UIImage", on: $0)?["m_Material"].targetID?.rawValue == "CAB-7468e80d83d3c28d17b61694968217a0:2719383334871231163"
        }, "Only the additive full-card and avatar overlays are removed from desktop photographs")
        check(card.node("playerHead").map { !card.artworkGlowNodeIDs.contains($0) } == true,
              "Suppressing source glow does not tint or hide the user avatar image")
        check(card.sprites["sprites"].array.allSatisfy { $0["texture"]["id"].string.map(textures.contains) == true },
              "Every selected card sprite has an exact source texture")
        let quit = desktop.scene.nodes.first { $0.name == "QuitBtn" }!
        let quitParent = desktop.scene.node(quit.parentID!)!
        check(!desktop.desktopHiddenNodeIDs.contains(quitParent.id) && !desktop.desktopHiddenNodeIDs.contains(card.parentID),
              "Desktop exposes the new source card and exit graphics")
        check(desktop.component("UIButton", on: quit.id)?["onClick"]["playerActionId"].string == "watch_exit_game",
              "The exit retains the original source button rather than a recreated glyph")
        let cached = try HUDSourceDesktopDocumentCache().document(resourceRoot: root)
        check(cached.desktopProfileCard != nil && cached.widgets == nil, "The cached desktop profile includes the immutable card only")
        return count
    }

    private static func verifyDesktopDocumentCache(root: URL) throws -> Int {
        final class Records {
            private let lock = NSLock()
            private var loads = 0
            private var values: [HUDSourceWatchDocument] = []
            private var errors: [Error] = []
            func nextLoad() -> Int { lock.lock(); defer { lock.unlock() }; loads += 1; return loads }
            func append(_ result: Result<HUDSourceWatchDocument, Error>) {
                lock.lock(); defer { lock.unlock() }
                switch result { case .success(let value): values.append(value); case .failure(let error): errors.append(error) }
            }
            var snapshot: (loads: Int, values: [HUDSourceWatchDocument], errors: [Error]) {
                lock.lock(); defer { lock.unlock() }; return (loads, values, errors)
            }
        }
        var checks = 0
        func check(_ value: Bool, _ message: String) { checks += 1; precondition(value, message) }
        let files = ["scene", "clips", "watch-blur", "sprites", "materials", "desktop-profile-card"]
        let temporary = FileManager.default.temporaryDirectory.appendingPathComponent("endfield-cache-" + UUID().uuidString)
        let alias = temporary.appendingPathExtension("alias")
        try FileManager.default.createDirectory(at: temporary, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: temporary); try? FileManager.default.removeItem(at: alias) }
        for file in files {
            try FileManager.default.copyItem(at: root.appendingPathComponent(file + ".json"),
                to: temporary.appendingPathComponent(file + ".json"))
        }
        try FileManager.default.createSymbolicLink(at: alias, withDestinationURL: temporary)
        func decode(_ url: URL) throws -> HUDSourceWatchDocument {
            try HUDSourceWatchDocument(resourceRoot: url, includeWidgets: false, includeSourceText: false)
        }
        func touch(_ file: String, _ step: Int) throws {
            try FileManager.default.setAttributes([.modificationDate: Date(timeIntervalSince1970: Double(2_000_000_000 + step))],
                ofItemAtPath: temporary.appendingPathComponent(file + ".json").path)
        }
        let records = Records()
        let cache = HUDSourceDesktopDocumentCache { url in _ = records.nextLoad(); return try decode(url) }
        var previous = try cache.document(resourceRoot: temporary)
        check(previous.widgets == nil && previous.fonts.object.isEmpty && previous.labels.object.isEmpty,
              "The shared document contains only the immutable desktop profile")
        check(try cache.document(resourceRoot: temporary) === previous, "Repeated desktop opens reuse the same immutable document")
        check(try cache.document(resourceRoot: alias) === previous && records.snapshot.loads == 1,
              "Canonical resource aliases cannot trigger duplicate parsing")
        for (index, file) in files.enumerated() {
            try touch(file, index)
            let next = try cache.document(resourceRoot: temporary)
            check(next !== previous && records.snapshot.loads == index + 2,
                  "Changing each desktop input invalidates the document: \(file)")
            previous = next
        }
        try Data("unused font data".utf8).write(to: temporary.appendingPathComponent("fonts.json"))
        check(try cache.document(resourceRoot: temporary) === previous,
              "Unused source font data does not invalidate the desktop profile")
        cache.clear()
        check(try cache.document(resourceRoot: temporary) !== previous, "Explicit clearing forces a fresh document")

        let failures = Records()
        let retry = HUDSourceDesktopDocumentCache { url in
            if failures.nextLoad() == 1 { throw HUDSourceError.invalid("Injected read failure") }
            return try decode(url)
        }
        do { _ = try retry.document(resourceRoot: temporary); check(false, "An unsuccessful decode must be reported") }
        catch { check(true, "An unsuccessful decode must be reported") }
        let recovered = try retry.document(resourceRoot: temporary)
        check(try retry.document(resourceRoot: temporary) === recovered && failures.snapshot.loads == 2,
              "Failure is not cached and a successful retry is shared")

        let concurrent = Records(), started = DispatchSemaphore(value: 0), release = DispatchSemaphore(value: 0)
        let shared = HUDSourceDesktopDocumentCache { url in
            _ = concurrent.nextLoad(); started.signal()
            guard release.wait(timeout: .now() + 15) == .success else { throw HUDSourceError.invalid("Cache test timed out") }
            return try decode(url)
        }
        let group = DispatchGroup()
        for _ in 0..<16 {
            group.enter()
            DispatchQueue.global().async {
                concurrent.append(Result { try shared.document(resourceRoot: temporary) }); group.leave()
            }
        }
        check(started.wait(timeout: .now() + 15) == .success, "Concurrent cold load starts")
        release.signal()
        check(group.wait(timeout: .now() + 30) == .success, "Concurrent readers all complete")
        let result = concurrent.snapshot
        check(result.loads == 1 && result.errors.isEmpty && result.values.count == 16
              && result.values.allSatisfy { $0 === result.values[0] },
              "Sixteen concurrent cold requests perform exactly one decode and share its identity")

        let clearing = Records(), clearStarted = DispatchSemaphore(value: 0), clearRelease = DispatchSemaphore(value: 0)
        let reset = HUDSourceDesktopDocumentCache { url in
            if clearing.nextLoad() == 1 {
                clearStarted.signal()
                guard clearRelease.wait(timeout: .now() + 15) == .success else { throw HUDSourceError.invalid("Cache test timed out") }
            }
            return try decode(url)
        }
        group.enter()
        DispatchQueue.global().async { clearing.append(Result { try reset.document(resourceRoot: temporary) }); group.leave() }
        check(clearStarted.wait(timeout: .now() + 15) == .success, "A load can overlap explicit clearing")
        reset.clear() // Must acquire metadata independently while decoding is blocked.
        clearRelease.signal()
        check(group.wait(timeout: .now() + 30) == .success, "Clearing does not strand an in-flight reader")
        let afterClear = try reset.document(resourceRoot: temporary), resetResult = clearing.snapshot
        check(resetResult.loads == 2 && resetResult.errors.isEmpty && resetResult.values.count == 1
              && afterClear !== resetResult.values[0], "An old in-flight result cannot repopulate the cleared cache")

        let changed = Records()
        let unstable = HUDSourceDesktopDocumentCache { url in
            let value = try decode(url)
            if changed.nextLoad() == 1 { try touch("scene", 100) }
            return value
        }
        do { _ = try unstable.document(resourceRoot: temporary); check(false, "Mixed resource generations must not be published") }
        catch { check(true, "Mixed resource generations must not be published") }
        _ = try unstable.document(resourceRoot: temporary)
        check(changed.snapshot.loads == 2, "A resource change during parsing can retry with a coherent identity")
        return checks
    }

    /// Desktop labels draw natively; source TMP metrics must not shift any
    /// surviving plate, glyph plane, mask or hit rectangle when omitted.
    private static func verifyDesktopLayoutWithoutSourceText(document: HUDSourceWatchDocument,
                                                             text: HUDSourceTextGeometry) throws -> Int {
        let desktop = try HUDSourceWatchDocument(resourceRoot: document.root, includeWidgets: false, includeSourceText: false)
        precondition(desktop.fonts.object.isEmpty && desktop.labels.object.isEmpty,
                     "Desktop construction must omit source font and localization payloads")
        let runtime = try HUDSourceJSON.decoder().decode(HUDSourceJSONValue.self,
            from: Data(contentsOf: document.root.appendingPathComponent("runtime-root-camera.json")))
        let cameraModel = try HUDSourceWatchCamera(runtimeRoot: runtime)
        let original = HUDSourceWatchLayout(document: document) { id, _ in try? text.preferredSize(on: id, literal: "") }
        let simplified = HUDSourceWatchLayout(document: desktop)
        let cases: [(SIMD2<Double>, SIMD3<Double>)] = [
            (SIMD2(1280, 800), .zero), (SIMD2(1728, 1080), SIMD3(-9, 6, 0)), (SIMD2(900, 900), SIMD3(8, -7, 0))]
        let phases: [(Double, Double?)] = [(0.1, nil), (0.5, nil), (document.animation.entrance.lastKeyTime, nil),
            (document.animation.entrance.lastKeyTime, 0.2)]
        let buttons = try HUDSourceWatchButtonAnimation(document: document)
        var checks = 1
        for (screen, euler) in cases {
            let camera = try cameraModel.frame(screenSize: screen, localRotation: HUDSourceWatchCamera.quaternion(eulerDegrees: euler))
            for entryCount in [10, 100] {
                let navigation = try HUDSourceDesktopNavigationLayout(document: document, entryCount: entryCount)
                for position in [0.0, 0.5, 1.0] {
                    for (entrance, exit) in phases {
                        var baseline = try document.animation.pose(entranceTime: entrance, ambientTime: 1,
                            exitTime: exit, canvasResolution: camera.layout.canvasSize)
                        buttons.reset(at: 0)
                        buttons.apply(to: &baseline, at: 0.15)
                        document.applyMacButtonAvailability(to: &baseline)
                        for id in document.desktopHiddenNodeIDs {
                            var override = baseline.transforms[id] ?? HUDSourceTransformOverride()
                            override.active = false; baseline.transforms[id] = override
                        }
                        var reduced = baseline
                        _ = try original.apply(to: &baseline, verticalNormalizedPosition: position,
                            worldRoot: camera.worldRoot, desktopNavigation: navigation)
                        _ = try simplified.apply(to: &reduced, verticalNormalizedPosition: position,
                            worldRoot: camera.worldRoot, desktopNavigation: navigation)
                        let expected = try document.scene.resolve(overrides: baseline.transforms)
                        let actual = try desktop.scene.resolve(overrides: reduced.transforms)
                        for (id, before) in expected {
                            guard let after = actual[id] else { fatalError("Missing desktop node: \(id)") }
                            guard before.activeInHierarchy == after.activeInHierarchy,
                                  !before.activeInHierarchy || (before.rect == after.rect && before.worldMatrix == after.worldMatrix) else {
                                fatalError("Omitting source text changed visible geometry: \(before.node.path), scroll=\(position), entrance=\(entrance)")
                            }
                        }
                        checks += 1
                    }
                }
            }
        }
        return checks
    }

}
