#!/usr/bin/env python3
"""Oracle for the Windows EHUDZ01 decoder (windows/packaging/ehudz01.hpp).

Imports the UNCHANGED macOS packager scripts/package-watch-resources.py and
records what its compressed()/decoded() functions produce and reject for
synthetic inputs, plus a synthetic staged runtime inventory in the exact
record format of its inventory() (runtime-inventory.json), so the Windows
decoder and inventory verifier are checked against the Mac source itself.

    python3 -I windows/tools/package_ehudz01_reference.py \
        > windows/packaging/tests/fixtures/ehudz01-source.json
"""
import base64
import hashlib
import importlib.util
import json
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "package-watch-resources.py"


def load_packager():
    spec = importlib.util.spec_from_file_location("package_watch_resources", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def pattern(length, seed):
    """Deterministic bytes that compress partially (no randomness)."""
    out = bytearray()
    x = seed & 0xFFFFFFFF
    while len(out) < length:
        x = (1103515245 * x + 12345) & 0x7FFFFFFF
        out.append((x >> 16) & 0x3F if len(out) % 7 else 0x41)
    return bytes(out)


def main():
    packager = load_packager()
    magic, maximum = packager.MAGIC, packager.MAX_RESOURCE_BYTES
    assert magic == b"EHUDZ01\0" and maximum == 128 * 1024 * 1024
    cases = []

    def record(name, payload):
        try:
            data = packager.decoded(payload)
            cases.append({"name": name, "payload": base64.b64encode(payload).decode(), "expect": "bytes",
                          "decoded_bytes": len(data), "decoded_sha256": hashlib.sha256(data).hexdigest(),
                          "identity": not payload.startswith(magic)})
        except (ValueError, zlib.error) as error:  # the Mac decoder's two failure types
            cases.append({"name": name, "payload": base64.b64encode(payload).decode(), "expect": "error", "reason": str(error)})

    sources = {
        "json-text": (json.dumps({"materials": [{"id": i, "name": "__ui_%d" % i, "value": i * 0.125} for i in range(400)]}) + "\n").encode(),
        "single-byte": b"\x7f",
        "zeros-64k": bytes(65536),
        "pattern-50k": pattern(50_000, 18),
        "float-words": b"".join(struct.pack("<f", i / 3.0) for i in range(5000)),
    }
    for name, data in sources.items():
        packed = packager.compressed(data)
        assert packager.decoded(packed) == data
        record("compressed-" + name, packed)
    # Bytes without the magic are returned unchanged (identity resources).
    record("identity-png-header", b"\x89PNG\r\n\x1a\n" + bytes(range(40)))
    record("identity-empty", b"")
    record("identity-near-magic", b"EHUDZ02\0" + bytes(32))
    # Rejections of the decoder.
    good = packager.compressed(sources["json-text"])
    body = good[16:]
    length = len(sources["json-text"])
    record("truncated-header", magic + b"\x05\x00\x00")
    record("zero-length", magic + struct.pack("<Q", 0) + body)
    record("over-maximum", magic + struct.pack("<Q", maximum + 1) + body)
    record("declared-longer", magic + struct.pack("<Q", length + 1) + body)
    record("declared-shorter", magic + struct.pack("<Q", length - 1) + body)
    record("trailing-bytes", good + b"\x00")
    record("truncated-stream", good[:-5])
    record("corrupt-stream", magic + struct.pack("<Q", length) + b"\xff" * 64)
    record("zlib-wrapped", magic + struct.pack("<Q", length) + zlib.compress(sources["json-text"]))
    record("magic-only", magic)
    # A payload of exactly the maximum declared size is accepted.
    big = bytes(maximum)
    compressor = zlib.compressobj(level=6, wbits=-15)
    maximal = magic + struct.pack("<Q", maximum) + compressor.compress(big) + compressor.flush()
    assert packager.decoded(maximal) == big
    record("maximum-length", maximal)

    # A synthetic staged runtime inventory in the records of inventory().
    files, records = {}, []
    for name, data in [("Scene/scene.json", sources["json-text"]), ("Meshes/Plane.json", b'{"vertices":[0,1,2]}\n'),
                       ("textures/a.bgra-mips.bin", sources["pattern-50k"]), ("Cursor/player-default-icon_mouse.png", b"\x89PNG\r\n" + bytes(64))]:
        packed = data
        if (name.endswith(".json") or name.endswith(".bin")) and name != "Meshes/Plane.json":
            candidate = packager.compressed(data)
            if len(candidate) < len(data):
                packed = candidate
        assert packager.decoded(packed) == data
        files[name] = packed
        records.append({"path": name, "source_bytes": len(data), "source_sha256": packager.digest(data),
                        "bytes": len(packed), "sha256": packager.digest(packed),
                        "encoding": "raw-deflate-v1" if packed is not data else "identity"})
    materials = sources["float-words"]
    materials_packed = packager.compressed(materials)
    files["runtime-materials.json"] = materials_packed
    derived = [{"path": "runtime-materials.json", "source_path": "materials.json", "source_sha256": packager.digest(b"source catalog"),
                "source_indices": [0, 2], "policy": "selected-original-json-tokens", "decoded_bytes": len(materials),
                "bytes": len(materials_packed), "sha256": packager.digest(materials_packed), "encoding": "raw-deflate-v1"}]
    files["runtime-selection.json"] = (json.dumps({"schema": 1}, sort_keys=True) + "\n").encode()
    manifest = {"schema": 1, "policy": "byte-exact-runtime-dependencies", "profile": "desktop", "files": records, "derived_files": derived,
                "source_tree_bytes": 1, "runtime_source_bytes": sum(r["source_bytes"] for r in records),
                "runtime_bytes": sum(r["bytes"] for r in records) + sum(r["bytes"] for r in derived)}
    files[packager.INVENTORY] = (json.dumps(manifest, indent=2, ensure_ascii=False) + "\n").encode()

    output = {"source": "scripts/package-watch-resources.py", "source_sha256": hashlib.sha256(SCRIPT.read_bytes()).hexdigest(),
              "magic": base64.b64encode(magic).decode(), "maximum_bytes": maximum, "cases": cases,
              "inventory": {name: base64.b64encode(data).decode() for name, data in sorted(files.items())}}
    json.dump(output, sys.stdout, indent=1, sort_keys=True)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
