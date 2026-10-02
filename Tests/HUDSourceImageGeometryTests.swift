import Foundation
import simd

enum HUDSourceImageGeometryTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String) { count += 1; if !condition { fatalError(message) } }
        func close(_ actual: Double, _ expected: Double, _ message: String, tolerance: Double = 1e-4) {
            check(actual.isFinite && abs(actual - expected) <= tolerance, message)
        }
        func area(_ mesh: HUDSourceImageMesh) -> Double {
            stride(from: 0, to: mesh.indices.count, by: 3).reduce(0) { sum, i in
                let p = mesh.indices[i..<i + 3].map { mesh.positions[Int($0)] }
                return sum + abs(Double((p[1].x - p[0].x) * (p[2].y - p[0].y) - (p[1].y - p[0].y) * (p[2].x - p[0].x))) * 0.5
            }
        }
        do {
            let document = try HUDSourceWatchDocument()
            let original = document.sprites["sprites"].array.first { $0["name"].string == "game_tool_icon" }!
            let texture = document.sprites["source_textures"].array.first { $0["id"].string == original["texture"]["id"].string }!
            let sprite = try HUDSourceImageGeometry.Sprite(source: original, texture: texture)
            close(sprite.size.x, 80, "Untrimmed sprite design width stays80")
            close(sprite.padding.x, 9.076120376586914, "Fractional source left trim is preserved")
            close(sprite.outer.x, 10.076120376586914 / 84, "UV comes from full source texture, not cropped PNG")
            let id = HUDSourceID(rawValue: "fixture:image")
            func image(_ fields: [String: HUDSourceJSONValue]) -> HUDSourceWatchComponent {
                HUDSourceWatchComponent(id: id, type: "MonoBehaviour", script: "UIImage", data: fields)
            }
            let rect = HUDSourceRect(origin: SIMD2(-40, -40), size: SIMD2(80, 80))
            let simple = try HUDSourceImageGeometry.build(image: image([:]), sprite: sprite, rect: rect, pivot: SIMD2(0.5, 0.5))
            close(Double(simple.positions[0].x), -40 + sprite.padding.x, "Original trim offsets design-plane geometry")
            close(Double(simple.positions[2].x), 40, "Trimmed far edge remains at original texture end")
            check(simple.indices == [0, 1, 2, 2, 3, 0], "Source UI quads keep negative-Z winding")
            let white = try HUDSourceImageGeometry.build(image: image([:]), sprite: nil, rect: rect, pivot: .zero)
            close(area(white), 6400, "A null Sprite uses the full rect and default white texture")
            let data = Data("{\"raw_sprite\":{\"m_Rect\":{\"width\":100,\"height\":100},\"m_Border\":{\"x\":10,\"y\":10,\"z\":10,\"w\":10},\"m_PixelsToUnits\":100},\"effective_render_data\":{\"textureRect\":{\"x\":0,\"y\":0,\"width\":100,\"height\":100},\"textureRectOffset\":{\"x\":0,\"y\":0}},\"packing\":{\"packing_rotation\":\"kSPRNone\"}}".utf8)
            let fixture = try HUDSourceJSON.decoder().decode(HUDSourceJSONValue.self, from: data)
            let textureData = Data("{\"id\":\"fixture:texture\",\"width\":100,\"height\":100}".utf8)
            let fixtureSprite = try HUDSourceImageGeometry.Sprite(source: fixture,
                texture: HUDSourceJSON.decoder().decode(HUDSourceJSONValue.self, from: textureData))
            let square = HUDSourceRect(origin: .zero, size: SIMD2(100, 100))
            let sliced = try HUDSourceImageGeometry.build(image: image(["m_Type": .number(1), "m_FillCenter": .bool(false)]),
                sprite: fixtureSprite, rect: square, pivot: .zero)
            check(sliced.positions.count == 32 && sliced.indices.count == 48, "Border-only nine-slice emits eight quads")
            close(area(sliced), 3600, "Ten-unit slice border excludes the80×80 center")
            for origin in 0..<4 { for clockwise in [true, false] {
                let fill = try HUDSourceImageGeometry.build(image: image(["m_Type": .number(3), "m_FillMethod": .number(4),
                    "m_FillOrigin": .number(Double(origin)), "m_FillClockwise": .bool(clockwise)]), sprite: fixtureSprite,
                    rect: square, pivot: .zero, fillAmount: 0.25)
                close(area(fill), 2500, "Every clockwise/counterclockwise radial360 origin fills one complete quarter")
                check(fill.positions.allSatisfy { $0.x >= 0 && $0.x <= 100 && $0.y >= 0 && $0.y <= 100 }, "Radial cuts stay inside the original rect")
            } }
            let empty = try HUDSourceImageGeometry.build(image: image(["m_Type": .number(3)]), sprite: fixtureSprite,
                rect: square, pivot: .zero, fillAmount: 0)
            check(empty.indices.isEmpty, "Zero fill produces no source triangles")
        } catch { fatalError("Source Image fixture failed: \(error)") }
        return count
    }
}
