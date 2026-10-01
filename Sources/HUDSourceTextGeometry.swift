import Foundation
import simd

/// Single-line TMP geometry from the exported font asset, not a system font.
/// Coordinates stay in the text RectTransform's local +Y-up design plane.
/// The supported Watch labels use normal weight, character UV mapping and no
/// markup. Shaping, alternate weights, tags, multiline layout, width adjustment,
/// ellipsis, and fallback-atlas submeshes are intentionally explicit errors.
/// Reference algorithms (the game's modified TMP version is not exported):
/// https://github.com/Unity-Technologies/uGUI/blob/main/com.unity.ugui/Runtime/TMP/TextMeshProUGUI.cs
/// https://github.com/Unity-Technologies/uGUI/blob/main/com.unity.ugui/Runtime/TMP/TMP_Text.cs
/// https://github.com/Unity-Technologies/uGUI/blob/main/com.unity.ugui/Runtime/TMP/TMP_ShaderUtilities.cs
struct HUDSourceTextGeometry {
    struct Mesh {
        let fontID: HUDSourceID
        let materialID: HUDSourceID
        let atlasID: HUDSourceID
        let literal: String
        let positions: [SIMD4<Float>]
        let normals: [SIMD3<Float>]
        let uv: [SIMD2<Float>]
        /// Unity mesh UV2, called `uv1` by the Metal vertex descriptor.
        let uv2: [SIMD2<Float>]
        let indices: [UInt32]
        let color: SIMD4<Float>
        let pointSize: Double
        let advance: Double
        let baseline: Double
        let ascender: Double
        let descender: Double
        let bounds: HUDSourceRect
    }

    private struct Glyph {
        let index: UInt32
        let width: Double
        let height: Double
        let bearingX: Double
        let bearingY: Double
        let advance: Double
        let atlasOrigin: SIMD2<Double>
        let atlasSize: SIMD2<Double>
        let scale: Double
        let atlasIndex: Int
    }

    private struct Character {
        let glyph: UInt32
        let scale: Double
    }

    private struct Font {
        let id: HUDSourceID
        let pointSize: Double
        let scale: Double
        let ascent: Double
        let descent: Double
        let capHeight: Double
        let baseline: Double
        let normalSpacing: Double
        let normalStyle: Double
        let atlasSize: SIMD2<Double>
        let atlasIDs: [HUDSourceID]
        let materialID: HUDSourceID
        let glyphs: [UInt32: Glyph]
        let characters: [UInt32: Character]
        let hasPairAdjustments: Bool
    }

    private struct Configuration {
        let component: HUDSourceWatchComponent
        let font: Font
        let rect: HUDSourceRect
        let literal: String
        let materialID: HUDSourceID
        let padding: Double
        let stylePadding: Double
        let horizontal: Int
        let vertical: Int
        let margin: SIMD4<Double>
        let orthographicMultiplier: Double
        let characterSpacing: Double
        let wordSpacing: Double
    }

    private struct PlacedGlyph {
        let glyph: Glyph
        let scale: Double
        let origin: Double
        let faceBaseline: Double
    }

    private struct Line {
        let glyphs: [PlacedGlyph]
        let advance: Double
        let ascent: Double
        let descent: Double
        let capHeight: Double
    }

    private let fonts: [HUDSourceID: Font]
    private let fontForSourceCNID: [Int: HUDSourceID]
    private let materials: [HUDSourceID: HUDSourceJSONValue]
    private let components: [HUDSourceID: HUDSourceWatchComponent]
    private let literals: [HUDSourceID: String]

    init(document: HUDSourceWatchDocument) throws {
        var loaded: [HUDSourceID: Font] = [:]
        for entry in document.fonts["fonts"].array {
            guard let id = entry["id"].string else { continue }
            let data = entry["data"], face = data["m_FaceInfo"]
            let glyphTable = data["m_GlyphTable"].array
            if glyphTable.isEmpty || data["m_AtlasTextures"].array.isEmpty {
                // Font-search descriptors and runtime-generated dynamic atlases
                // have no serialized texture. Selecting one still fails below.
                continue
            }
            guard let material = data["material"].targetID,
                  face["m_PointSize"].float() > 0,
                  data["m_AtlasWidth"].float() > 0, data["m_AtlasHeight"].float() > 0 else {
                throw HUDSourceError.invalid("Incomplete source TMP font: \(id)")
            }
            var glyphs: [UInt32: Glyph] = [:]
            for record in glyphTable {
                guard let index = UInt32(exactly: record["m_Index"].float()),
                      let atlasIndex = Int(exactly: record["m_AtlasIndex"].float()), glyphs[index] == nil else {
                    throw HUDSourceError.invalid("Invalid/duplicate source TMP glyph in \(id)")
                }
                let metrics = record["m_Metrics"], atlas = record["m_GlyphRect"]
                glyphs[index] = Glyph(index: index, width: metrics["m_Width"].float(), height: metrics["m_Height"].float(),
                    bearingX: metrics["m_HorizontalBearingX"].float(), bearingY: metrics["m_HorizontalBearingY"].float(),
                    advance: metrics["m_HorizontalAdvance"].float(), atlasOrigin: SIMD2(atlas["m_X"].float(), atlas["m_Y"].float()),
                    atlasSize: SIMD2(atlas["m_Width"].float(), atlas["m_Height"].float()), scale: record["m_Scale"].float(1), atlasIndex: atlasIndex)
            }
            var characters: [UInt32: Character] = [:]
            for record in data["m_CharacterTable"].array {
                guard let unicode = UInt32(exactly: record["m_Unicode"].float()),
                      let index = UInt32(exactly: record["m_GlyphIndex"].float()), glyphs[index] != nil,
                      characters[unicode] == nil else {
                    throw HUDSourceError.invalid("Invalid/duplicate source TMP character in \(id)")
                }
                characters[unicode] = Character(glyph: index, scale: record["m_Scale"].float(1))
            }
            // Preserve atlas array order; missing references cannot shift an index.
            let atlasIDs = try data["m_AtlasTextures"].array.map { pointer -> HUDSourceID in
                guard let id = pointer.targetID else { throw HUDSourceError.invalid("Unresolved source TMP atlas in \(id)") }
                return id
            }
            guard !atlasIDs.isEmpty else { throw HUDSourceError.invalid("Missing source TMP atlas: \(id)") }
            let fontID = HUDSourceID(rawValue: id)
            loaded[fontID] = Font(id: fontID, pointSize: face["m_PointSize"].float(), scale: face["m_Scale"].float(1),
                ascent: face["m_AscentLine"].float(), descent: face["m_DescentLine"].float(), capHeight: face["m_CapLine"].float(),
                baseline: face["m_Baseline"].float(), normalSpacing: data["normalSpacingOffset"].float(), normalStyle: data["normalStyle"].float(),
                atlasSize: SIMD2(data["m_AtlasWidth"].float(), data["m_AtlasHeight"].float()), atlasIDs: atlasIDs,
                materialID: material, glyphs: glyphs, characters: characters,
                hasPairAdjustments: !data["m_FontFeatureTable"]["m_GlyphPairAdjustmentRecords"].array.isEmpty ||
                    !data["m_KerningTable"]["kerningPairs"].array.isEmpty)
        }
        fonts = loaded
        var lookup: [Int: HUDSourceID] = [:]
        for match in document.fonts["source_cn_id_matches"].array {
            if let source = Int(exactly: match["source_cn_font_id"].float()), let target = match["object_id"].string {
                let id = HUDSourceID(rawValue: target)
                guard loaded[id] != nil, lookup[source] == nil || lookup[source] == id else {
                    throw HUDSourceError.invalid("Missing/ambiguous source CN font mapping: \(source)")
                }
                lookup[source] = id
            }
        }
        fontForSourceCNID = lookup
        var loadedMaterials: [HUDSourceID: HUDSourceJSONValue] = [:]
        for record in document.materials["materials"].array {
            if let id = record["id"].string { loadedMaterials[HUDSourceID(rawValue: id)] = record["data"] }
        }
        materials = loadedMaterials
        var textComponents: [HUDSourceID: HUDSourceWatchComponent] = [:]
        for (node, records) in document.components {
            if let component = records.first(where: { $0.kind == "UIText" && $0.enabled }) { textComponents[node] = component }
        }
        components = textComponents
        var joined: [HUDSourceID: String] = [:]
        for label in document.labels["nodes"].array {
            if let node = label["node_id"].string, let literal = label["cn_literal"].string {
                joined[HUDSourceID(rawValue: node)] = literal
            }
        }
        literals = joined
    }

    func literal(on node: HUDSourceID) -> String? {
        if let value = literals[node] { return value }
        guard let value = components[node]?["m_text"].string, !value.isEmpty else { return nil }
        return value
    }

    /// `sdfScale` is TMP's Canvas/lossy-Y-scale multiplier. WorldSpace and
    /// ScreenSpaceCamera with a camera use abs(lossyScaleY); Overlay uses its
    /// ratio to canvasScaleFactor. It must come from the same source transform
    /// used for rendering, and must be updated when the animated scale changes.
    /// `materialID` permits an evidenced runtime font-material assignment.
    /// Mesh.color is a batch tint; vertices are white unless the caller chooses
    /// otherwise. CanvasGroup alpha and animated m_fontColor stay at the caller.
    func build(on node: HUDSourceID, rect: HUDSourceRect, sdfScale: Double,
               materialID: HUDSourceID? = nil, literal replacement: String? = nil) throws -> Mesh {
        guard let component = components[node], let text = replacement ?? literal(on: node) else {
            throw HUDSourceError.invalid("Unresolved source Watch text: \(node.rawValue)")
        }
        guard sdfScale.isFinite, sdfScale >= 0 else { throw HUDSourceError.invalid("Invalid source TMP SDF scale") }
        let configuration = try configuration(component: component, literal: text, rect: rect, materialID: materialID)
        let pointSize = try fittedPointSize(configuration)
        let line = try measure(configuration, at: pointSize)
        return try mesh(configuration, line: line, pointSize: pointSize, sdfScale: sdfScale)
    }

    private func configuration(component: HUDSourceWatchComponent, literal: String, rect: HUDSourceRect,
                               materialID: HUDSourceID?) throws -> Configuration {
        guard rect.origin.x.isFinite, rect.origin.y.isFinite, rect.size.x.isFinite, rect.size.y.isFinite,
              rect.size.x > 0, rect.size.y > 0 else { throw HUDSourceError.invalid("Invalid source TMP text rectangle") }
        guard !literal.unicodeScalars.contains(where: { scalar in
            scalar.value < 32 || scalar.value == 0x2028 || scalar.value == 0x2029 ||
                scalar.properties.generalCategory == .nonspacingMark || scalar.properties.generalCategory == .spacingMark ||
                scalar.properties.generalCategory == .enclosingMark
        }) else { throw HUDSourceError.invalid("Source TMP multiline/control/shaping layout is unsupported") }
        guard !component["m_isRichText"].flag() || !literal.contains("<"),
              component["m_fontStyle"].float() == 0, component["m_fontWeight"].float(400) == 400,
              !component["m_isRightToLeft"].flag(), !component["m_isVolumetricText"].flag(),
              !component["m_enableVertexGradient"].flag(), component["m_charWidthMaxAdj"].float() == 0,
              component["m_horizontalMapping"].float() == 0, component["m_verticalMapping"].float() == 0,
              component["m_geometrySortingOrder"].float() == 0, component["m_overflowMode"].float() == 0 else {
            throw HUDSourceError.invalid("Source TMP tags/style/gradient/width adjustment/mapping/overflow mode is unsupported")
        }
        let explicitFont = component["m_serializedFontAsset"].targetID
        let sourceCNID = Int(exactly: component["sourceCNFontId"].float())
        guard let fontID = explicitFont ?? sourceCNID.flatMap({ fontForSourceCNID[$0] }), let font = fonts[fontID] else {
            throw HUDSourceError.invalid("Unresolved source TMP font")
        }
        guard !font.hasPairAdjustments || !component["m_enableKerning"].flag() else {
            throw HUDSourceError.invalid("Source TMP kerning feature table is unsupported")
        }
        let selectedMaterial = materialID ?? component["m_fontMaterial"].targetID ??
            component["m_serializedSharedMaterial"].targetID ?? component["m_Material"].targetID ?? font.materialID
        guard let material = materials[selectedMaterial] else {
            throw HUDSourceError.invalid("Missing source TMP material: \(selectedMaterial.rawValue)")
        }
        let values = Self.materialFloats(material)
        guard let gradient = values["_GradientScale"], gradient > 0 else { throw HUDSourceError.invalid("Source TMP material is not SDF") }
        let padding = Self.materialPadding(material, values: values, extra: component["m_enableExtraPadding"].flag())
        let stylePadding = font.normalStyle * 0.25 * gradient * (values["_ScaleRatioA"] ?? 1)
        let horizontal = Int(component["m_HorizontalAlignment"].float(1))
        let vertical = Int(component["m_VerticalAlignment"].float(256))
        guard [1, 2, 4, 32].contains(horizontal), [256, 512, 1024, 2048, 4096, 8192].contains(vertical) else {
            throw HUDSourceError.invalid("Source TMP justified/unknown alignment is unsupported")
        }
        let margin = component["m_margin"]
        return Configuration(component: component, font: font, rect: rect, literal: literal, materialID: selectedMaterial,
            padding: min(padding, max(0, gradient - stylePadding)), stylePadding: stylePadding,
            horizontal: horizontal, vertical: vertical,
            margin: SIMD4(margin["x"].float(), margin["y"].float(), margin["z"].float(), margin["w"].float()),
            orthographicMultiplier: component["m_isOrthographic"].flag(true) ? 1 : 0.1,
            characterSpacing: component["m_characterSpacing"].float(), wordSpacing: component["m_wordSpacing"].float())
    }

    private func measure(_ configuration: Configuration, at pointSize: Double) throws -> Line {
        let font = configuration.font
        let baseScale = pointSize / font.pointSize * font.scale * configuration.orthographicMultiplier
        let emScale = pointSize * 0.01 * configuration.orthographicMultiplier
        let spacing = (font.normalSpacing + configuration.characterSpacing) * emScale
        var placed: [PlacedGlyph] = []
        var advance = 0.0, ascent = -Double.infinity, descent = Double.infinity, capHeight = 0.0
        for scalar in configuration.literal.unicodeScalars {
            guard let character = font.characters[scalar.value], let glyph = font.glyphs[character.glyph] else {
                throw HUDSourceError.invalid("Source TMP glyph is missing: U+\(String(scalar.value, radix: 16, uppercase: true))")
            }
            let scale = baseScale * character.scale * glyph.scale
            guard scale.isFinite, scale > 0, glyph.atlasIndex >= 0, glyph.atlasIndex < font.atlasIDs.count else {
                throw HUDSourceError.invalid("Invalid source TMP glyph scale/atlas")
            }
            if !CharacterSet.whitespaces.contains(scalar) && glyph.width > 0 && glyph.height > 0 {
                placed.append(PlacedGlyph(glyph: glyph, scale: scale, origin: advance,
                    faceBaseline: font.baseline * baseScale * font.scale))
            }
            ascent = max(ascent, font.ascent * scale)
            descent = min(descent, font.descent * scale)
            capHeight = max(capHeight, font.capHeight * scale)
            advance += glyph.advance * scale + spacing
            if CharacterSet.whitespaces.contains(scalar) { advance += configuration.wordSpacing * emScale }
        }
        if !configuration.literal.isEmpty { advance -= spacing }
        return Line(glyphs: placed, advance: advance, ascent: ascent.isFinite ? ascent : 0,
            descent: descent.isFinite ? descent : 0, capHeight: capHeight)
    }

    /// The Watch's unwrapped, uncompressed one-line case of TMP's bounded
    /// 0.05-point search. No line-breaking or auto-width approximation is made.
    private func fittedPointSize(_ configuration: Configuration) throws -> Double {
        let component = configuration.component
        let auto = component["m_enableAutoSizing"].flag()
        let minimum = auto ? component["m_fontSizeMin"].float() : component["m_fontSize"].float()
        let maximum = auto ? component["m_fontSizeMax"].float() : minimum
        guard minimum.isFinite, maximum.isFinite, minimum > 0, maximum >= minimum else {
            throw HUDSourceError.invalid("Invalid source TMP point-size bounds")
        }
        let width = configuration.rect.size.x - configuration.margin.x - configuration.margin.z
        let height = configuration.rect.size.y - configuration.margin.y - configuration.margin.w
        guard width > 0, height > 0 else { throw HUDSourceError.invalid("Invalid source TMP margins") }
        var lower = minimum, upper = maximum
        var pointSize = min(maximum, max(minimum, component["m_fontSize"].float(maximum)))
        for _ in 0..<100 {
            let line = try measure(configuration, at: pointSize)
            let tooLarge = line.advance > width + 0.0001 || line.ascent - line.descent > height + 0.0001
            if auto && tooLarge && pointSize > minimum {
                upper = pointSize
                let step = max((pointSize - lower) / 2, 0.05)
                pointSize = max(minimum, floor((pointSize - step) * 20 + 0.5) / 20)
            } else if auto && !tooLarge && upper - lower > 0.051 && pointSize < maximum {
                lower = pointSize
                let step = max((upper - pointSize) / 2, 0.05)
                pointSize = min(maximum, floor((pointSize + step) * 20 + 0.5) / 20)
            } else {
                guard !tooLarge else { throw HUDSourceError.invalid("Source TMP text requires unsupported wrapping/overflow layout") }
                return pointSize
            }
        }
        throw HUDSourceError.invalid("Source TMP auto-size search did not converge")
    }

    private func mesh(_ configuration: Configuration, line: Line, pointSize: Double, sdfScale: Double) throws -> Mesh {
        let rect = configuration.rect, margin = configuration.margin
        let left = rect.origin.x + margin.x
        let width = rect.size.x - margin.x - margin.z
        var x = left
        switch configuration.horizontal {
        case 2: x += (width - line.advance) / 2
        case 4: x += width - line.advance
        default: break
        }
        let bottom = rect.origin.y, top = bottom + rect.size.y, middle = (bottom + top) / 2
        var baseline: Double
        switch configuration.vertical {
        case 256: baseline = top - margin.y - line.ascent
        case 512: baseline = middle - (line.ascent + margin.y + line.descent - margin.w) / 2
        case 1024: baseline = bottom + margin.w - line.descent
        case 2048: baseline = middle
        case 8192: baseline = middle - (line.capHeight - margin.y - margin.w) / 2
        default: baseline = middle // Geometry alignment is resolved after extents.
        }
        var positions: [SIMD4<Float>] = [], uv: [SIMD2<Float>] = [], uv2: [SIMD2<Float>] = [], indices: [UInt32] = []
        var minXY = SIMD2<Double>(repeating: .infinity), maxXY = SIMD2<Double>(repeating: -.infinity)
        var atlasID: HUDSourceID?
        for placed in line.glyphs {
            let glyph = placed.glyph, scale = placed.scale
            let thisAtlas = configuration.font.atlasIDs[glyph.atlasIndex]
            guard atlasID == nil || atlasID == thisAtlas else { throw HUDSourceError.invalid("Source TMP fallback/multi-atlas submeshes are unsupported") }
            atlasID = thisAtlas
            let horizontalPad = configuration.padding + configuration.stylePadding
            let glyphLeft = placed.origin + (glyph.bearingX - horizontalPad) * scale
            let glyphRight = glyphLeft + (glyph.width + 2 * horizontalPad) * scale
            let glyphTop = placed.faceBaseline + (glyph.bearingY + configuration.padding) * scale
            let glyphBottom = glyphTop - (glyph.height + 2 * configuration.padding) * scale
            let corners = [SIMD2(glyphLeft, glyphBottom), SIMD2(glyphLeft, glyphTop), SIMD2(glyphRight, glyphTop), SIMD2(glyphRight, glyphBottom)]
            let uvMin = (glyph.atlasOrigin - SIMD2<Double>(repeating: horizontalPad)) / configuration.font.atlasSize
            let uvMax = (glyph.atlasOrigin + glyph.atlasSize + SIMD2<Double>(repeating: horizontalPad)) / configuration.font.atlasSize
            let textureCorners = [SIMD2(uvMin.x, uvMin.y), SIMD2(uvMin.x, uvMax.y), SIMD2(uvMax.x, uvMax.y), SIMD2(uvMax.x, uvMin.y)]
            let faceUV = [SIMD2<Double>(0, 0), SIMD2<Double>(0, 1), SIMD2<Double>(1, 1), SIMD2<Double>(1, 0)]
            let start = UInt32(positions.count)
            for corner in 0..<4 {
                let p = corners[corner]
                minXY = simd_min(minXY, p); maxXY = simd_max(maxXY, p)
                positions.append(SIMD4(Float(p.x + x), Float(p.y + baseline), 0, 1))
                uv.append(SIMD2(Float(textureCorners[corner].x), Float(textureCorners[corner].y)))
                // Face/outline UVs use TMP's 9-bit quantization and 4096 stride.
                let packed = Double(Int(faceUV[corner].x * 511)) * 4096 + Double(Int(faceUV[corner].y * 511))
                uv2.append(SIMD2(Float(packed), Float(scale * sdfScale)))
            }
            indices.append(contentsOf: [start, start + 1, start + 2, start + 2, start + 3, start])
        }
        if positions.isEmpty { minXY = .zero; maxXY = .zero }
        if configuration.horizontal == 32 {
            let shift = left + width / 2 - (minXY.x + maxXY.x) / 2 - x
            x += shift
            for index in positions.indices { positions[index].x += Float(shift) }
        }
        if configuration.vertical == 4096 {
            let target = middle - (maxXY.y + margin.y + minXY.y - margin.w) / 2
            for index in positions.indices { positions[index].y += Float(target - baseline) }
            baseline = target
        }
        guard let atlas = atlasID ?? configuration.font.atlasIDs.first else { throw HUDSourceError.invalid("Missing source TMP atlas") }
        return Mesh(fontID: configuration.font.id, materialID: configuration.materialID, atlasID: atlas, literal: configuration.literal,
            positions: positions, normals: Array(repeating: SIMD3<Float>(0, 0, -1), count: positions.count), uv: uv, uv2: uv2, indices: indices,
            color: Self.color32(configuration.component["m_fontColor"].color), pointSize: pointSize, advance: line.advance,
            baseline: baseline, ascender: line.ascent, descender: line.descent,
            bounds: HUDSourceRect(origin: minXY + SIMD2(x, baseline), size: maxXY - minXY))
    }

    private static func materialFloats(_ material: HUDSourceJSONValue) -> [String: Double] {
        var values: [String: Double] = [:]
        for pair in material["m_SavedProperties"]["m_Floats"].array {
            let entry = pair.array
            if entry.count == 2, let key = entry[0].string, let value = entry[1].number { values[key] = value }
        }
        return values
    }

    private static func materialPadding(_ material: HUDSourceJSONValue, values: [String: Double], extra: Bool) -> Double {
        let keywords = Set(material["m_ValidKeywords"].array.compactMap { $0.string })
        let ratioA = values["_ScaleRatioA"] ?? 1, ratioB = values["_ScaleRatioB"] ?? 1, ratioC = values["_ScaleRatioC"] ?? 1
        let face = (values["_FaceDilate"] ?? 0) * ratioA
        var extent = max(0, face + ((values["_OutlineWidth"] ?? 0) + (values["_OutlineSoftness"] ?? 0)) * ratioA)
        if keywords.contains("GLOW_ON") {
            extent = max(extent, face + ((values["_GlowOffset"] ?? 0) + (values["_GlowOuter"] ?? 0)) * ratioB)
        }
        if keywords.contains("UNDERLAY_ON") || keywords.contains("UNDERLAY_INNER") {
            let offset = max(abs(values["_UnderlayOffsetX"] ?? 0), abs(values["_UnderlayOffsetY"] ?? 0))
            extent = max(extent, face + ((values["_UnderlayDilate"] ?? 0) + (values["_UnderlaySoftness"] ?? 0) + offset) * ratioC)
        }
        return min(1, extent + (extra ? 4 : 0)) * (values["_GradientScale"] ?? 0) + 1.25
    }

    private static func color32(_ value: SIMD4<Float>) -> SIMD4<Float> {
        var result = value
        for index in 0..<4 { result[index] = (min(1, max(0, value[index])) * 255).rounded() / 255 }
        return result
    }
}
