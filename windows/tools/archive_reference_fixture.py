#!/usr/bin/env python3
"""Compact actual detached Mac layer witnesses; never copy bitmap assets.

Input must be a completed archive_reference.sh export and its independent
archive_text_reference.sh export. No expected native geometry is reconstructed.
Every source/export blob is pinned before reduction. The resulting fixture is
test-only, not a runtime UI tree or a replacement for original Mac sources.
"""
import argparse
import hashlib
import json
import pathlib

PIN = "ca04f142185c7de40acd8523bdb563195d90a1d1"
MAXIMUM = 32 * 1024 * 1024
IDENTITY = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]


def read(path):
    raw = path.read_bytes()
    if len(raw) > MAXIMUM:
        raise ValueError("Oversized original fixture")
    return json.loads(raw), hashlib.sha256(raw).hexdigest()


def color(value):
    if value is None:
        return None
    # Retain both original color-space components and exported sRGB. Mac gray
    # conversion has small floating point differences from its source constant.
    return {key: value[key] for key in ("sourceColorSpace", "sourceComponents", "sRGB")}


def node(original):
    output = {key: original[key] for key in (
        "kind", "frame", "bounds", "name", "opacity", "hidden",
        "cornerRadius", "masksToBounds", "allowsGroupOpacity", "contentsScale",
        "contentsGravity", "borderWidth", "zPosition")}
    for key in ("backgroundColor", "borderColor"):
        output[key] = color(original[key])
    output["contents"] = original["contents"]
    if original.get("mask") is not None:
        output["mask"] = node(original["mask"])
    for key in ("transform", "sublayerTransform"):
        if original[key] != IDENTITY:
            output[key] = original[key]
    if "shape" in original:
        shape = dict(original["shape"])
        for key in ("fillColor", "strokeColor"):
            shape[key] = color(shape[key])
        output["shape"] = shape
    if "text" in original:
        text = dict(original["text"])
        text["foregroundColor"] = color(text["foregroundColor"])
        output["text"] = text
    output["children"] = [node(child) for child in original["children"]]
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive_root", type=pathlib.Path)
    parser.add_argument("text_root", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError("Refusing to overwrite an existing compact source fixture")
    repo = pathlib.Path(__file__).resolve().parents[2]
    provenance, provenance_hash = read(args.archive_root / "provenance.json")
    if provenance["sourceAuthority"] != PIN or provenance["windowsCreated"] or provenance["mediaOpened"]:
        raise ValueError("Wrong source authority or non-detached fixture")
    for path, expected in {**provenance["sourceSHA256"], **provenance["exporterSHA256"]}.items():
        if hashlib.sha256((repo / path).read_bytes()).hexdigest() != expected:
            raise ValueError("Original source/exporter changed: " + path)
    index, index_hash = read(args.archive_root / "archive-reference.json")
    if index["windowCreated"] or index["mediaOpened"] or index["schemaVersion"] != 1:
        raise ValueError("Unexpected source export")
    rows = []
    nodes, identities = [], {}

    def intern(value):
        # Bottom-up immutable structural deduplication retains paint order and
        # every varying frame/clip/path. References are fixture indices only.
        value = dict(value)
        value["children"] = [intern(child) for child in value["children"]]
        if "mask" in value:
            value["mask"] = intern(value["mask"])
        encoded = json.dumps(value, ensure_ascii=False, separators=(",", ":"), sort_keys=True)
        if encoded not in identities:
            identities[encoded] = len(nodes)
            nodes.append(value)
        return identities[encoded]
    for descriptor in index["entries"]:
        path = args.archive_root / descriptor["file"]
        if path.parent != args.archive_root or path.suffix != ".json":
            raise ValueError("Unconfined original export path")
        original, original_hash = read(path)
        row = {key: value for key, value in original.items() if key != "root"}
        row["root"] = intern(node(original["root"]))
        row["originalBlobSHA256"] = original_hash
        rows.append(row)
    text, text_hash = read(args.text_root / "archive-text-reference.json")
    text_provenance, _ = read(args.text_root / "provenance.json")
    if text_provenance["sourceAuthority"] != PIN or text["windowsCreated"]:
        raise ValueError("Wrong detached text reference")
    output = {"schemaVersion": 1, "sourceAuthority": PIN,
              "sourceSHA256": provenance["sourceSHA256"],
              "exporterSHA256": provenance["exporterSHA256"],
              "provenanceSHA256": provenance_hash, "sourceIndexSHA256": index_hash,
              "textReferenceSHA256": text_hash, "textReference": text,
              "rasterAssets": index["rasterAssets"], "nodes": nodes, "states": rows,
              "limitations": index["limitations"], "windowsCreated": False,
              "mediaOpened": False, "compaction": "Original layer defaults/matrices omitted; frame/clip/paint/text/path order retained. Root/children/mask are immutable nodes-array indices, interned bottom-up. Raster descriptors only; bitmap bytes are not duplicated."}
    encoded = json.dumps(output, ensure_ascii=False, separators=(",", ":"), sort_keys=True).encode("utf8") + b"\n"
    if len(encoded) > 2 * 1024 * 1024:
        raise ValueError("Compact fixture exceeds bounded test metadata")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("xb") as handle:
        handle.write(encoded)
    print(f"PASS {len(rows)} original Archive states; {len(encoded)} bytes; no copied rasters")


if __name__ == "__main__":
    main()
