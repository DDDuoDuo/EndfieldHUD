#!/usr/bin/env python3
"""Extract the MoneyCell's original numeric SDF glyphs, without a font dependency.

The source branch remains authoritative. This retains its original R8 samples
and metrics for the wallet and its recovery tooltip; it does not resample or substitute a font.
Run with --check to verify the committed subset against the original assets.
"""
import argparse
import base64
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "Resources/WatchSource"
FONT_ID = "CAB-e1b9246317f3a094cb651bec654f7b76:-5452070126780473065"
ATLAS = "Textures/DefaultFont_CN_3500_Atlas--e1b92463--8016279098669342999.bin"
OUTPUT = SOURCE / "Scene/account-numerals.json"


def extract():
    fonts_data = (SOURCE / "Scene/fonts.json").read_bytes()
    fonts = json.loads(fonts_data)
    face = next(font for font in fonts["fonts"] if font["id"] == FONT_ID)["data"]
    scene_data = (SOURCE / "Scene/scene.json").read_bytes()
    cell = next(node for node in json.loads(scene_data)["nodes"]
                if node["path"].endswith("MoneyCellRoot/MoneyCell/ContentNode/Text"))
    text = next(component["data"] for component in cell["components"] if component.get("script") == "UIText")
    assert text["sourceCNFontId"] == -430371804
    assert any(match["source_cn_font_id"] == -430371804 and match["object_id"] == FONT_ID
               for match in fonts["source_cn_id_matches"])
    raw = (SOURCE / ATLAS).read_bytes()
    atlas_width, atlas_height = face["m_AtlasWidth"], face["m_AtlasHeight"]
    assert len(raw) == atlas_width * atlas_height
    characters = {row["m_Unicode"]: row for row in face["m_CharacterTable"]}
    glyphs = {row["m_Index"]: row for row in face["m_GlyphTable"]}
    padding, output = 4, []
    for character in dict.fromkeys("0123456789/ —:：下次回复全部Next recoveryFull"):
        glyph = glyphs[characters[ord(character)]["m_GlyphIndex"]]
        metrics, rect = glyph["m_Metrics"], glyph["m_GlyphRect"]
        assert glyph["m_AtlasIndex"] == 0 and glyph["m_Scale"] == characters[ord(character)]["m_Scale"] == 1
        width = rect["m_Width"] + padding * 2 if character != " " else 0
        height = rect["m_Height"] + padding * 2 if character != " " else 0
        x, y = rect["m_X"] - padding, rect["m_Y"] - padding
        assert 0 <= x and x + width <= atlas_width and 0 <= y and y + height <= atlas_height
        # Unity's raw atlas origin is bottom-left. Store each small glyph's
        # rows top-first for the retained native image's screen coordinates.
        samples = b"".join(raw[row * atlas_width + x:row * atlas_width + x + width]
                           for row in range(y + height - 1, y - 1, -1))
        output.append({"character": character, "width": metrics["m_Width"], "height": metrics["m_Height"],
                       "bearingX": metrics["m_HorizontalBearingX"], "bearingY": metrics["m_HorizontalBearingY"],
                       "advance": metrics["m_HorizontalAdvance"], "columns": width, "rows": height,
                       "samples": base64.b64encode(samples).decode("ascii")})
    result = {"schema": 1, "family": face["m_FaceInfo"]["m_FamilyName"], "style": face["m_FaceInfo"]["m_StyleName"],
              "pointSize": face["m_FaceInfo"]["m_PointSize"], "padding": padding, "gradientScale": 5,
              "fontID": FONT_ID, "sourceNode": cell["path"], "sourceFontID": text["sourceCNFontId"],
              "fontSourceSHA256": hashlib.sha256(fonts_data).hexdigest(),
              "atlasSourceSHA256": hashlib.sha256(raw).hexdigest(), "atlasSource": ATLAS,
              "notice": "Original-game HarmonyOS Sans SC Medium glyph subset from origin/codex/endfield-watch-motion. Retains source glyph metrics and R8 SDF samples; original asset ownership is unchanged.",
              "glyphs": output}
    return (json.dumps(result, ensure_ascii=False, separators=(",", ":")) + "\n").encode()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    subset = extract()
    if args.check:
        if OUTPUT.read_bytes() != subset:
            raise SystemExit("Account numeric subset differs from its original font/atlas")
        print(f"PASS: {len(json.loads(subset)['glyphs'])} exact source wallet glyphs, {len(subset):,} bytes; original font and atlas hashes verified")
    else:
        OUTPUT.write_bytes(subset)
        print(f"Extracted {len(subset):,} bytes to {OUTPUT}")
