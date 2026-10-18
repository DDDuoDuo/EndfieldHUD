#!/usr/bin/env python3
"""Export the Mac desktop Map occlusion silhouettes of the two lower buttons.

While the Map is presented, the Mac (HUDSourceWatchView.desktopMapOcclusion
and SystemHUDView.applySourceMapOcclusion) masks the module content with the
even-odd path of its bounds and the projected plates of the two navigation
buttons below the circle, Storage (/TechtreeBtn/Icon) and Activity Monitor
(/ReportBtn/Icon), and gives presses inside those plates to the buttons
(mapFilteredButton, mouseDown navigationTargetAtDesignPoint). The plates are
HUDSourceWatchView.makeDesktopBottomSilhouettes: the boundary loops of each
Icon's sprite mesh (decoded_render_mesh), normalized to its RectTransform as
vertex * pixelsToUnits / size + pivot (Unity y up).

Reads the unchanged Mac source at the pinned authority:
  * Sources/HUDSourceWatchView.swift              the silhouette algorithm (checked present)
  * Sources/SystemHUDView.swift                   the even-odd mask and press precedence (checked present)
  * Resources/WatchSource/Scene/scene.json        the Icon nodes and their UIImage components
  * Resources/WatchSource/Scene/sprites.json      the bound sprites and their render meshes
The Windows runtime package keeps no sprite meshes, so the loops are written
to the fixture below and embedded in windows/core/map_source_cutout.cpp
(--cpp prints that table); map_cutout_tests compares the two.

No app, defaults, window, network or user data is touched.
Usage: map_cutout_reference.py NEW-output.json [--cpp]
"""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
AUTHORITY = "ca04f142185c7de40acd8523bdb563195d90a1d1"
VIEW = "Sources/HUDSourceWatchView.swift"
HUD = "Sources/SystemHUDView.swift"
SCENE = "Resources/WatchSource/Scene/scene.json"
SPRITES = "Resources/WatchSource/Scene/sprites.json"
SUFFIXES = [("/TechtreeBtn/Icon", "storage"), ("/ReportBtn/Icon", "activityMonitor")]


def show(path):
    return subprocess.run(["git", "-C", str(ROOT), "show", f"{AUTHORITY}:{path}"], check=True, capture_output=True).stdout


def loops(sprite):
    raw, mesh = sprite["raw_sprite"], sprite["decoded_render_mesh"]
    width, height = raw["m_Rect"]["width"], raw["m_Rect"]["height"]
    ppu = raw.get("m_PixelsToUnits", 100)
    pivot = raw["m_Pivot"]
    if not (width > 0 and height > 0 and ppu > 0):
        raise SystemExit("Invalid sprite rect")
    # SIMD2(x, y) * ppu / size + pivot, in this order (identical doubles).
    vertices = [((v[0] * ppu) / width + pivot["x"], (v[1] * ppu) / height + pivot["y"]) for v in mesh["vertices"]]
    triangles = [t for submesh in mesh["triangles"] for t in submesh]
    if not triangles or any(len(t) != 3 or any(not 0 <= i < len(vertices) for i in t) for t in triangles):
        raise SystemExit("Invalid sprite mesh")
    n = len(vertices)
    key = lambda a, b: min(a, b) * n + max(a, b)
    counts, directed = {}, []
    for t in triangles:
        for i in range(3):
            a, b = t[i], t[(i + 1) % 3]
            counts[key(a, b)] = counts.get(key(a, b), 0) + 1
            directed.append((a, b))
    edges = [e for e in directed if counts[key(*e)] == 1]
    result = []
    while edges:
        first = edges.pop(0)
        loop, following = [first[0]], first[1]
        while following != first[0]:
            loop.append(following)
            index = next((k for k, e in enumerate(edges) if e[0] == following), None)
            if index is None:
                raise SystemExit("Open sprite mesh boundary")
            following = edges.pop(index)[1]
        if len(loop) < 3:
            raise SystemExit("Degenerate sprite mesh boundary")
        result.append([list(vertices[i]) for i in loop])
    return result


def main():
    if len(sys.argv) not in (2, 3) or (len(sys.argv) == 3 and sys.argv[2] != "--cpp"):
        raise SystemExit(__doc__)
    view, hud = show(VIEW).decode(), show(HUD).decode()
    for text, needle in ((view, '"/TechtreeBtn/Icon", "/ReportBtn/Icon"'), (view, "decoded_render_mesh"),
                         (view, "func mapFilteredButton"), (view, "(frame.inheritedAlpha[silhouette.nodeID] ?? 1) > 0.001"),
                         (hud, "mask.fillRule = .evenOdd"), (hud, "if mapIsInteractive, navigationTargetAtDesignPoint(p) == nil")):
        if needle not in text:
            raise SystemExit(f"Authority silhouette algorithm changed: {needle}")
    scene_bytes, sprite_bytes = show(SCENE), show(SPRITES)
    scene, sprites = json.loads(scene_bytes), json.loads(sprite_bytes)
    joined = {}
    for sprite in sprites["sprites"]:
        for binding in sprite.get("bindings", []):
            if binding.get("component_id"):
                joined[binding["component_id"]] = sprite
    silhouettes = []
    for suffix, module in SUFFIXES:
        node = next(n for n in scene["nodes"] if n["path"].endswith(suffix))
        image = next(c for c in node["components"] if c.get("script") == "UIImage")
        if int(image["data"]["m_Type"]) != 0 or image["data"]["m_PreserveAspect"]:
            raise SystemExit("Silhouette image is no longer a simple stretched sprite")
        sprite = joined[image["id"]]
        silhouettes.append({"node": suffix, "module": module, "sprite": sprite["name"],
                            "size": [node["transform"]["raw"]["m_SizeDelta"]["x"], node["transform"]["raw"]["m_SizeDelta"]["y"]],
                            "loops": loops(sprite)})
    out = {"authority": AUTHORITY,
           "sources": {SCENE: hashlib.sha256(scene_bytes).hexdigest(), SPRITES: hashlib.sha256(sprite_bytes).hexdigest()},
           "silhouettes": silhouettes}
    Path(sys.argv[1]).write_text(json.dumps(out, indent=1) + "\n")
    if len(sys.argv) == 3:
        for s in silhouettes:
            print(f'// {s["node"]} ({s["sprite"]})')
            for loop in s["loops"]:
                print("{{" + ",".join("{" + repr(x) + "," + repr(y) + "}" for x, y in loop) + "}," + str(len(loop)) + "},")


if __name__ == "__main__":
    main()
