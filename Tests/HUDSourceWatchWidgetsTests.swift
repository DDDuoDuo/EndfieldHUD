import Foundation
import simd

/// Actual published source widget resources; no Metal, screen or account APIs.
enum HUDSourceWatchWidgetsTests {
    static func run() -> Int {
        var count = 0
        func check(_ value: Bool, _ message: String) {
            count += 1; if !value { fatalError(message) }
        }
        func id(_ path: String) -> HUDSourceID {
            HUDSourceID(rawValue: "CAB-7979328e8a85d73c8b989cdca5a79bf8:" + path)
        }
        do {
            let document = try HUDSourceWatchDocument()
            guard let widget = document.widgets else { fatalError("Runtime-loaded original Watch widget missing") }
            let original = try HUDSourceWatchDocument(includeWidgets: false)
            let root = id("-8536182027554143077")
            let parent = HUDSourceID(rawValue: "CAB-194e41a66c2317b9df19269f505210be:7686435424458337495")
            check(widget.sourceScene.nodes.count == 101 && widget.sourceScene.rootID == root,
                "The complete original BP13 hierarchy preserves all101 source node identities")
            check(original.scene.node(parent)?.childIDs == [] && document.scene.node(parent)?.childIDs == [root],
                "Original empty PlayInfoPosNode mounts exactly one separately loaded source prefab")
            check(document.scene.node(root)?.parentID == parent && document.buttons.count == original.buttons.count,
                "Dynamic profile buttons do not become main-menu availability entries")
            guard let source = document.scene.node(root) else { fatalError("Mounted source root missing") }
            check(source.transform.localPosition == HUDSourceVector3(0, 0, 0) &&
                source.transform.localRotation == .identity && source.transform.localScale == HUDSourceVector3(1, 1, 1),
                "CreateObject worldPositionStays=false retains original prefab localTRS")
            let resolved = try document.scene.resolve()
            check(resolved[root]?.rect?.size == SIMD2<Double>(364, 128) && resolved[parent]?.rect?.size == SIMD2<Double>(492, 164),
                "Source business-card dimensions remain364x128 inside the492x164 anchor slot")
            var wider = HUDSourceTransformOverride(); wider.sizeDelta = HUDSourceVector2(928, 412)
            check(try document.scene.resolve(overrides: [parent: wider])[root]?.rect?.size == SIMD2<Double>(364, 128),
                "Changing the parent slot cannot stretch the centered source prefab")
            check(document.component("CanvasGroup", on: root)?["m_IgnoreParentGroups"].flag() == false,
                "The loaded card retains original Watch parent alpha inheritance")
            var inherited = HUDSourceWatchPose(transforms: [:]); inherited.properties[parent] = ["m_Alpha": 0.4]
            check(abs((document.inheritedAlpha(pose: inherited)[root] ?? -1) - 0.4) < 1e-12,
                "Watch's original parent animation alpha reaches the loaded card")
            check(document.components[root]?.contains(where: { $0.kind == "UIButton" }) == true &&
                widget.profileButtonIDs.contains(root) && widget.profileButtonIDs.contains(id("-1259455786877270885")),
                "Source whole-card and avatar UIButton targets are available for desktop actions")
            for (componentPath, spriteID) in [
                ("-7953100122619591525", "CAB-14ad054d0b3ecb6415a9d756f0373a0d:-315661501574421994"),
                ("6971995041116963995", "CAB-6229f6df6b60eb7378a63d43f2cc476f:-7674256598449963272"),
                ("9177187169683424411", "CAB-f217e169ee1f06211c9412161331ea4b:1206393168504411954")
            ] {
                guard let shared = document.spriteByComponent[id(componentPath)],
                      let base = original.sprites["sprites"].array.first(where: { $0["id"].string == spriteID }) else {
                    fatalError("Shared original Sprite binding disappeared when mounting the widget")
                }
                check(shared["id"].string == spriteID,
                    "Added shared Sprite component resolves to its precise original source ID")
                check(shared["png"]["file"].string == base["png"]["file"].string &&
                    shared["png"]["sha256"].string == base["png"]["sha256"].string &&
                    shared["texture"]["id"].string == base["texture"]["id"].string &&
                    shared["raw_sprite"]["m_Rect"]["width"].number == base["raw_sprite"]["m_Rect"]["width"].number &&
                    shared["raw_sprite"]["m_Rect"]["height"].number == base["raw_sprite"]["m_Rect"]["height"].number,
                    "Shared Sprite joining retains the base artwork bytes, asset path, texture and design dimensions")
            }

            var empty = HUDSourceWatchPose(transforms: [:])
            let unaccounted = try widget.apply(to: &empty, state: .desktopReference, at: 0)
            check(unaccounted.text[id("5452048441616942235")] == "" &&
                unaccounted.text[id("-1279796635999091557")] == "" &&
                unaccounted.text[id("-271944461426309989")] == "",
                "A desktop artwork reference does not invent game name, UID or level")
            check(empty.transforms[id("2608809364581631131")]?.active == false &&
                empty.transforms[id("1439449805994247323")]?.active == false &&
                empty.transforms[id("-1343216166854802277")]?.active == false &&
                empty.transforms[id("5176234855714047131")]?.active == false,
                "Source hidden head-level/activity/season gates and unproved birthday state stay hidden")
            check(unaccounted.sprites[widget.bannerImageComponentID] == "CAB-453a2bda20bf8e297e653335ba1e9836:4250958573640483290",
                "The default banner is one explicit Yvonne source artwork, not a table-wide account rotation")
            var pose = HUDSourceWatchPose(transforms: [:])
            let reference = try widget.apply(to: &pose, state: .recordReference, at: 8.234)
            check(reference.text[id("5452048441616942235")] == "管理员" &&
                reference.text[id("-271944461426309989")] == "60" &&
                reference.text[id("-8926847511235017573")] == "满级" &&
                reference.text[id("-1279796635999091557")] == "",
                "The controlled fixture uses generic text and original level/max literals without personal UID")
            check(pose.value("m_FillAmount", on: id("-3381093159146109797"), fallback: -1) == 1 &&
                reference.sprites[HUDSourceID(rawValue: "CAB-7979328e8a85d73c8b989cdca5a79bf8:-192639394979658597")] ==
                    "CAB-1d0a4c08b5a9c475680941da45a0073d:-2158241429177752369" &&
                reference.sprites[HUDSourceID(rawValue: "CAB-7979328e8a85d73c8b989cdca5a79bf8:8114856523223581851")] ==
                    "CAB-63c7ff925764ef7e901f962df477c835:8364725078580218544",
                "Explicit maximum-level/avatar/frame state targets the original source components")
            var alternate = HUDSourceWatchWidgets.State.recordReference
            alternate.bannerArtwork = "weapon_typhoeus_banner"
            alternate.profile.isMaximumLevel = false
            alternate.profile.relativeExperience = 10; alternate.profile.nextLevelExperience = 40
            let changed = try widget.apply(to: &pose, state: alternate, at: 12)
            check(changed.sprites[widget.bannerImageComponentID] == "CAB-520854379894316bbed598088030cd9c:5980436975975208562" &&
                changed.text[id("-8926847511235017573")] == "10/40" &&
                pose.value("m_FillAmount", on: id("-3381093159146109797"), fallback: -1) == 0.25,
                "Changing explicit artwork/experience updates the original image and fill channels")
            let text = try HUDSourceTextGeometry(document: document)
            for (node, literal) in reference.text where !literal.isEmpty {
                guard let bounds = resolved[node]?.rect else { fatalError("Original profile text rect missing") }
                let mesh = try text.build(on: node, rect: bounds, sdfScale: 1, literal: literal)
                check(mesh.positions.count == literal.unicodeScalars.count * 4 &&
                    mesh.atlasID.rawValue == "CAB-e1b9246317f3a094cb651bec654f7b76:8016279098669342999",
                    "Added profile text uses the original CN3500 glyph metrics/atlas with no system-font fallback")
            }
            let levelLabel = id("-2360844232722889573")
            guard let labelRect = resolved[levelLabel]?.rect else { fatalError("Original level-label rect missing") }
            let labelMesh = try text.build(on: levelLabel, rect: labelRect, sdfScale: 1)
            check(text.literal(on: levelLabel) == "权限等阶" && labelMesh.positions.count == 16 &&
                labelMesh.atlasID.rawValue == "CAB-e1b9246317f3a094cb651bec654f7b76:8016279098669342999",
                "Original CN-table level label uses four source atlas glyphs")
        } catch { fatalError("Source widget resource/geometry checks failed: \(error)") }
        return count
    }
}
