import Foundation
import simd

struct HUDSourceImageMesh {
    var positions: [SIMD4<Float>] = []
    var uv: [SIMD2<Float>] = []
    var indices: [UInt32] = []
    mutating func quad(_ xy: [SIMD2<Double>], _ texture: [SIMD2<Double>]) {
        let start = UInt32(positions.count)
        positions.append(contentsOf: xy.map { SIMD4(Float($0.x), Float($0.y), 0, 1) })
        uv.append(contentsOf: texture.map { SIMD2(Float($0.x), Float($0.y)) })
        indices.append(contentsOf: [start, start + 1, start + 2, start + 2, start + 3, start])
    }
}

/// Unity uGUI Image geometry rules, using the exported sprite rect, trim,
/// border and original texture UV coordinates. World-space canvases do not
/// perform screen pixel snapping. The source mesh flag is zero for all 392
/// Watch images; tight sprite polygons are therefore not used as UI quads.
/// Algorithm reference: Unity-Technologies/uGUI 5.6 Image.cs (MIT), pinned
/// 3394d36ec68d9cba7152c4fce48b6eebaf8a941b. See WatchSource/NOTICE.txt.
enum HUDSourceImageGeometry {
    struct Sprite {
        let size: SIMD2<Double>
        let padding: SIMD4<Double>
        let border: SIMD4<Double>
        let outer: SIMD4<Double>
        let inner: SIMD4<Double>
        let pixelsPerUnit: Double
        let textureID: String

        init(source: HUDSourceJSONValue, texture: HUDSourceJSONValue) throws {
            let raw = source["raw_sprite"], rd = source["effective_render_data"]
            let spriteRect = raw["m_Rect"], textureRect = rd["textureRect"]
            size = SIMD2(spriteRect["width"].float(), spriteRect["height"].float())
            let offset = rd["textureRectOffset"].vector2
            padding = SIMD4(offset.x, offset.y,
                size.x - offset.x - textureRect["width"].float(),
                size.y - offset.y - textureRect["height"].float())
            let b = raw["m_Border"]
            border = SIMD4(b["x"].float(), b["y"].float(), b["z"].float(), b["w"].float())
            let width = texture["width"].float(), height = texture["height"].float()
            guard size.x > 0, size.y > 0, width > 0, height > 0,
                  source["packing"]["packing_rotation"].string == "kSPRNone",
                  let id = texture["id"].string else {
                throw HUDSourceError.invalid("Unsupported/incomplete original sprite")
            }
            outer = SIMD4(textureRect["x"].float() / width, textureRect["y"].float() / height,
                (textureRect["x"].float() + textureRect["width"].float()) / width,
                (textureRect["y"].float() + textureRect["height"].float()) / height)
            inner = SIMD4(outer.x + (border.x - padding.x) / width,
                outer.y + (border.y - padding.y) / height,
                outer.z - (border.z - padding.z) / width,
                outer.w - (border.w - padding.w) / height)
            pixelsPerUnit = raw["m_PixelsToUnits"].float(100)
            textureID = id
        }
    }

    static func build(image: HUDSourceWatchComponent, sprite: Sprite?, rect: HUDSourceRect,
                      pivot: SIMD2<Double>, canvasReferencePPU: Double = 100,
                      fillAmount: Double? = nil) throws -> HUDSourceImageMesh {
        guard rect.size.x > 0, rect.size.y > 0 else { return HUDSourceImageMesh() }
        guard !image["m_UseSpriteMesh"].flag() else {
            throw HUDSourceError.invalid("Unexpected UIImage sprite-mesh flag")
        }
        var mesh = HUDSourceImageMesh()
        guard let sprite else {
            mesh.quad(points(rect.origin, rect.origin + rect.size), points(.zero, SIMD2(1, 1)))
            return mesh
        }
        let type = Int(image["m_Type"].float())
        let uv = points(SIMD2(sprite.outer.x, sprite.outer.y), SIMD2(sprite.outer.z, sprite.outer.w))
        if type == 0 || (type == 1 && sprite.border == .zero) {
            mesh.quad(drawingPoints(sprite: sprite, rect: rect, pivot: pivot,
                preserveAspect: type == 0 && image["m_PreserveAspect"].flag()), uv)
        } else if type == 1 || type == 2 {
            let ppu = sprite.pixelsPerUnit / canvasReferencePPU * image["m_PixelsPerUnitMultiplier"].float(1)
            guard ppu > 0 else { throw HUDSourceError.invalid("Invalid source sprite pixelsPerUnit") }
            var border = sprite.border / ppu
            for axis in 0..<2 {
                let combined = border[axis] + border[axis + 2]
                if combined > rect.size[axis] && combined != 0 {
                    let ratio = rect.size[axis] / combined
                    border[axis] *= ratio; border[axis + 2] *= ratio
                }
            }
            let padding = sprite.padding / ppu
            let x = [rect.origin.x + padding.x, rect.origin.x + border.x,
                     rect.origin.x + rect.size.x - border.z, rect.origin.x + rect.size.x - padding.z]
            let y = [rect.origin.y + padding.y, rect.origin.y + border.y,
                     rect.origin.y + rect.size.y - border.w, rect.origin.y + rect.size.y - padding.w]
            let u = [sprite.outer.x, sprite.inner.x, sprite.inner.z, sprite.outer.z]
            let v = [sprite.outer.y, sprite.inner.y, sprite.inner.w, sprite.outer.w]
            let tileSize = SIMD2(max(0, sprite.size.x - sprite.border.x - sprite.border.z),
                                 max(0, sprite.size.y - sprite.border.y - sprite.border.w)) / ppu
            for row in 0..<3 { for column in 0..<3 {
                if column == 1 && row == 1 && !image["m_FillCenter"].flag(true) { continue }
                guard x[column + 1] > x[column], y[row + 1] > y[row] else { continue }
                let tileX = type == 2 && column == 1
                let tileY = type == 2 && row == 1
                let sx = tileX && tileSize.x > 0 ? tileSize.x : x[column + 1] - x[column]
                let sy = tileY && tileSize.y > 0 ? tileSize.y : y[row + 1] - y[row]
                let countX = max(1, Int(ceil((x[column + 1] - x[column]) / sx)))
                let countY = max(1, Int(ceil((y[row + 1] - y[row]) / sy)))
                guard countX * countY <= 16250 else { throw HUDSourceError.invalid("Source tiled image exceeds Unity mesh limit") }
                for iy in 0..<countY { for ix in 0..<countX {
                    let low = SIMD2(x[column] + Double(ix) * sx, y[row] + Double(iy) * sy)
                    let high = SIMD2(min(low.x + sx, x[column + 1]), min(low.y + sy, y[row + 1]))
                    let uvLow = SIMD2(u[column], v[row])
                    let uvHigh = SIMD2(lerp(u[column], u[column + 1], (high.x - low.x) / sx),
                                       lerp(v[row], v[row + 1], (high.y - low.y) / sy))
                    mesh.quad(points(low, high), points(uvLow, uvHigh))
                } }
            } }
        } else if type == 3 {
            let amount = min(1, max(0, fillAmount ?? image["m_FillAmount"].float(1)))
            guard amount >= 0.001 else { return mesh }
            let method = Int(image["m_FillMethod"].float()), origin = Int(image["m_FillOrigin"].float())
            let clockwise = image["m_FillClockwise"].flag(true)
            let xy = drawingPoints(sprite: sprite, rect: rect, pivot: pivot, preserveAspect: image["m_PreserveAspect"].flag())
            if amount >= 1 { mesh.quad(xy, uv); return mesh }
            if method == 0 || method == 1 {
                let axis = method, start = origin == 1 ? 1 - amount : 0, end = origin == 1 ? 1 : amount
                var low = SIMD2<Double>.zero, high = SIMD2<Double>(repeating: 1)
                low[axis] = start; high[axis] = end
                mesh.quad(subQuad(xy, low, high), subQuad(uv, low, high))
            } else if method == 2 {
                appendRadial(to: &mesh, xy: xy, uv: uv, amount: amount, invert: clockwise, corner: origin % 4)
            } else if method == 3 {
                for side in 0..<2 {
                    let even = origin > 1 ? 1 : 0
                    let low: SIMD2<Double>, high: SIMD2<Double>
                    if origin == 0 || origin == 2 {
                        low = SIMD2(side == even ? 0 : 0.5, 0); high = SIMD2(side == even ? 0.5 : 1, 1)
                    } else {
                        low = SIMD2(0, side == even ? 0.5 : 0); high = SIMD2(1, side == even ? 1 : 0.5)
                    }
                    let part = amount * 2 - Double(clockwise ? side : 1 - side)
                    appendRadial(to: &mesh, xy: subQuad(xy, low, high), uv: subQuad(uv, low, high),
                        amount: min(1, max(0, part)), invert: clockwise, corner: (side + origin + 3) % 4)
                }
            } else if method == 4 {
                for corner in 0..<4 {
                    let low = SIMD2<Double>(corner < 2 ? 0 : 0.5, corner == 0 || corner == 3 ? 0 : 0.5)
                    let high = low + SIMD2<Double>(repeating: 0.5)
                    let part = amount * 4 - Double(clockwise ? (corner + origin) % 4 : 3 - (corner + origin) % 4)
                    appendRadial(to: &mesh, xy: subQuad(xy, low, high), uv: subQuad(uv, low, high),
                        amount: min(1, max(0, part)), invert: clockwise, corner: (corner + 2) % 4)
                }
            } else { throw HUDSourceError.invalid("Unsupported source fill method") }
        } else { throw HUDSourceError.invalid("Unsupported source image type") }
        return mesh
    }

    private static func drawingPoints(sprite: Sprite, rect: HUDSourceRect, pivot: SIMD2<Double>, preserveAspect: Bool) -> [SIMD2<Double>] {
        var origin = rect.origin, size = rect.size
        if preserveAspect {
            let ratio = sprite.size.x / sprite.size.y
            if ratio > size.x / size.y { let old = size.y; size.y = size.x / ratio; origin.y += (old - size.y) * pivot.y }
            else { let old = size.x; size.x = size.y * ratio; origin.x += (old - size.x) * pivot.x }
        }
        let w = sprite.size.x.rounded(.toNearestOrEven), h = sprite.size.y.rounded(.toNearestOrEven)
        let low = origin + size * SIMD2(sprite.padding.x / w, sprite.padding.y / h)
        let high = origin + size * SIMD2((w - sprite.padding.z) / w, (h - sprite.padding.w) / h)
        return points(low, high)
    }
    private static func points(_ low: SIMD2<Double>, _ high: SIMD2<Double>) -> [SIMD2<Double>] {
        [low, SIMD2(low.x, high.y), high, SIMD2(high.x, low.y)]
    }
    private static func subQuad(_ q: [SIMD2<Double>], _ low: SIMD2<Double>, _ high: SIMD2<Double>) -> [SIMD2<Double>] {
        points(q[0] + (q[2] - q[0]) * low, q[0] + (q[2] - q[0]) * high)
    }
    private static func lerp(_ a: Double, _ b: Double, _ t: Double) -> Double { a + (b - a) * t }
    private static func appendRadial(to mesh: inout HUDSourceImageMesh, xy: [SIMD2<Double>], uv: [SIMD2<Double>],
                                     amount: Double, invert: Bool, corner: Int) {
        guard amount >= 0.001 else { return }
        let invert = corner & 1 == 1 ? !invert : invert
        if !invert && amount > 0.999 { mesh.quad(xy, uv); return }
        let angle = (invert ? 1 - amount : amount) * .pi / 2
        mesh.quad(radialCut(xy, cosine: cos(angle), sine: sin(angle), invert: invert, corner: corner),
                  radialCut(uv, cosine: cos(angle), sine: sin(angle), invert: invert, corner: corner))
    }
    private static func radialCut(_ points: [SIMD2<Double>], cosine: Double, sine: Double, invert: Bool, corner: Int) -> [SIMD2<Double>] {
        var q = points, cosine = cosine, sine = sine
        let i0 = corner, i1 = (corner + 1) % 4, i2 = (corner + 2) % 4, i3 = (corner + 3) % 4
        if corner & 1 == 1 {
            if sine > cosine {
                cosine /= sine; sine = 1
                if invert { q[i1].x = lerp(q[i0].x, q[i2].x, cosine); q[i2].x = q[i1].x }
            } else if cosine > sine {
                sine /= cosine; cosine = 1
                if !invert { q[i2].y = lerp(q[i0].y, q[i2].y, sine); q[i3].y = q[i2].y }
            } else { cosine = 1; sine = 1 }
            if !invert { q[i3].x = lerp(q[i0].x, q[i2].x, cosine) }
            else { q[i1].y = lerp(q[i0].y, q[i2].y, sine) }
        } else {
            if cosine > sine {
                sine /= cosine; cosine = 1
                if !invert { q[i1].y = lerp(q[i0].y, q[i2].y, sine); q[i2].y = q[i1].y }
            } else if sine > cosine {
                cosine /= sine; sine = 1
                if invert { q[i2].x = lerp(q[i0].x, q[i2].x, cosine); q[i3].x = q[i2].x }
            } else { cosine = 1; sine = 1 }
            if invert { q[i3].y = lerp(q[i0].y, q[i2].y, sine) }
            else { q[i1].x = lerp(q[i0].x, q[i2].x, cosine) }
        }
        return q
    }
}
