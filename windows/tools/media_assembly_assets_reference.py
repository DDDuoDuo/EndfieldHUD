#!/usr/bin/env python3
"""Copy only the active PhotoMode catalog from the authoritative Mac commit."""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path

COMMIT = "ca04f142185c7de40acd8523bdb563195d90a1d1"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]

    def original(path):
        return subprocess.check_output(["git", "show", f"{COMMIT}:{path}"], cwd=repo)

    model = original("Sources/MediaAssemblyModel.swift").decode()
    filters = re.findall(r'case \w+ = "([a-z0-9_]+)"', model.split("enum MediaAssemblyFilter:")[1].split("var title:")[0])
    stickers = re.findall(r'case \w+ = "([a-z0-9_]+)"', model.split("enum MediaAssemblyStickerKind:")[1].split("var title:")[0])
    assert len(filters) == 14 and len(stickers) == 24
    provenance = original("Resources/MediaAssembly/provenance.json")
    pinned = {entry["file"]: entry for entry in json.loads(provenance)["files"]}
    records = []
    for group, names, extension in [("luts", filters, "rgb8"), ("filter-icons", filters, "png"), ("stickers", stickers, "png")]:
        for name in names:
            relative = f"{group}/{name}.{extension}"
            data = original("Resources/MediaAssembly/" + relative)
            sha = hashlib.sha256(data).hexdigest()
            assert sha == pinned[relative]["sha256"]
            if group == "luts":
                assert len(data) == 32 * 32 * 32 * 3
            target = args.output / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            records.append({"path": relative, "bytes": len(data), "sha256": sha})
    (args.output / "provenance.json").write_bytes(provenance)
    catalog = json.dumps({"version": 1, "sourceCommit": COMMIT, "files": records}, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode() + b"\n"
    (args.output / "catalog.json").write_bytes(catalog)
    print(json.dumps({"files": len(records), "bytes": sum(x["bytes"] for x in records), "catalogSHA256": hashlib.sha256(catalog).hexdigest()}))


if __name__ == "__main__":
    main()
