#!/usr/bin/env python3
"""Guard the stable desktop/data contract while replacing its visual shell.

This reads repository files only. --behavioral additionally runs the existing
core suite, whose persistence cases use temporary stores and defaults suites.
It never starts the application or reads the user's Application Support folder.
"""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "Tests/Fixtures/stable-integration-contract.json"

# The approved 1.1.0 release changes version metadata, not the stable bundle,
# preferences, permission or signed-update identity recorded in the baseline.
RELEASE_METADATA = {"CFBundleShortVersionString": "1.1.0", "CFBundleVersion": "12"}


# Exact user-requested copy changes; every replacement remains required.
ALLOWED_TRANSLATION_UPDATES = {
    'Entry(". Drag to pan. Scroll or pinch to zoom. Right-click to place a pin. Arrow keys move the map.", "。拖动平移，滚动或捏合缩放，右键放置标记，方向键移动地图。", "。拖動平移，滾動或捏合縮放，右鍵放置標記，方向鍵移動地圖。", "。ドラッグで移動、スクロールまたはピンチで拡大縮小、右クリックでピンを配置、矢印キーでマップを移動します。")':
        'Entry(". Drag to pan. Scroll or pinch to zoom. Right-click to place or remove a pin. Left-click to hide coordinates. Arrow keys move the map.", "。拖动平移，滚动或捏合缩放，右键放置或移除标记，左键隐藏坐标，方向键移动地图。", "。拖動平移，滾動或捏合縮放，右鍵放置或移除標記，左鍵隱藏座標，方向鍵移動地圖。", "。ドラッグで移動、スクロールまたはピンチで拡大縮小、右クリックでピンを配置または削除、左クリックで座標を非表示、矢印キーでマップを移動します。")',
    'Entry("Memory ", "内存 ", "記憶體 ", "メモリ ")':
        'Entry("RAM ", "RAM ", "RAM ", "RAM ")',
    'Entry("Memory", "内存", "記憶體", "メモリ")':
        'Entry("RAM", "RAM", "RAM", "RAM")',
}


def entries(source):
    return {line.strip().removesuffix(",") for line in source.splitlines()
            if line.strip().startswith("Entry(")}


def check(root, baseline):
    failures = []
    updates = baseline.get("reviewedBehaviorUpdates", {})
    # Persistence files cannot be exempted by these explicit behavior changes.
    allowed_updates = {"Sources/WorkModeFocusController.swift", "Sources/WorldMapGeometry.swift",
                       "Sources/AppActivityMonitor.swift", "Sources/SystemActivityMonitor.swift",
                       "Sources/Localization.swift"}
    for name, update in updates.items():
        if name not in allowed_updates or update.get("baselineSha256") != baseline["files"].get(name) or not update.get("reason"):
            failures.append(f"Invalid reviewed behavior update: {name}")
    for name, expected in baseline["files"].items():
        if name in updates and name in allowed_updates:
            expected = updates[name]["sha256"]
        source = root / name
        if not source.is_file():
            failures.append(f"Missing stable functional file: {name}")
        else:
            data = source.read_bytes()
            if name == "Sources/Models.swift":
                # Add one language without exempting preferences, saved keys,
                # defaults, existing enum values or any other model behavior.
                data = data.replace(b"    case japanese\n    case korean\n", b"    case japanese\n", 1)
            if hashlib.sha256(data).hexdigest() != expected:
                failures.append(f"Stable behavior/data contract changed: {name}")
    try:
        info = plistlib.loads((root / "Resources/Info.plist").read_bytes())
        for key, expected in baseline["infoPlist"].items():
            if key in RELEASE_METADATA:
                expected = RELEASE_METADATA[key]
            if key == "CFBundleLocalizations":
                expected = expected + ["ko"]
            if info.get(key) != expected:
                failures.append(f"Stable application/update identity changed: {key}")
        if info.get("HUDReleaseTag") != "v" + RELEASE_METADATA["CFBundleShortVersionString"]:
            failures.append("Release tag does not match the approved version: HUDReleaseTag")
    except (OSError, ValueError, plistlib.InvalidFileException) as error:
        failures.append(f"Cannot inspect Info.plist: {error}")
    try:
        current = entries((root / "Sources/LocalizationCatalog.swift").read_text())
        translation_updates = baseline.get("reviewedTranslationUpdates", {})
        for original, update in translation_updates.items():
            if (original not in baseline["translations"]
                    or original not in ALLOWED_TRANSLATION_UPDATES
                    or update.get("entry") != ALLOWED_TRANSLATION_UPDATES[original]
                    or not update.get("reason")):
                failures.append(f"Invalid reviewed translation update: {original[:100]}")
        for entry in baseline["translations"]:
            replacement = translation_updates.get(entry, {}).get("entry", entry)
            if replacement not in current:
                failures.append(f"Stable translated text changed or removed: {entry[:100]}")
    except OSError as error:
        failures.append(f"Cannot inspect localization catalog: {error}")
    return failures


def self_test(baseline):
    # Mutations stay entirely inside this owned temporary checkout fixture.
    with tempfile.TemporaryDirectory(prefix="EndfieldHUD-Compatibility-") as temporary:
        root = Path(temporary)
        names = list(baseline["files"]) + ["Resources/Info.plist", "Sources/LocalizationCatalog.swift"]
        for name in names:
            destination = root / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, destination)
        assert not check(root, baseline), "Current repository must pass before mutation checks"
        store = root / "Sources/UserProfileStore.swift"
        original = store.read_bytes()
        store.write_bytes(original.replace(b"EndfieldCharge/Profile", b"EndfieldHUD/Profile"))
        assert any("UserProfileStore" in failure for failure in check(root, baseline)), "A changed data path must fail"
        store.write_bytes(original)
        plist = root / "Resources/Info.plist"
        original = plist.read_bytes()
        values = plistlib.loads(original)
        values["CFBundleIdentifier"] = "example.incompatible"
        plist.write_bytes(plistlib.dumps(values))
        assert any("CFBundleIdentifier" in failure for failure in check(root, baseline)), "A changed defaults/permission identity must fail"
        plist.write_bytes(original)
        for key, invalid in (("CFBundleVersion", "999"), ("HUDReleaseTag", "v0.0.0"),
                             ("SUFeedURL", "https://example.invalid/appcast.xml")):
            values = plistlib.loads(original)
            values[key] = invalid
            plist.write_bytes(plistlib.dumps(values))
            assert any(key in failure for failure in check(root, baseline)), "Release metadata and update identity remain guarded"
            plist.write_bytes(original)
        values = plistlib.loads(original)
        values["CFBundleLocalizations"].remove("en")
        plist.write_bytes(plistlib.dumps(values))
        assert any("CFBundleLocalizations" in failure for failure in check(root, baseline)), "Adding Korean cannot remove an existing bundle language"
        plist.write_bytes(original)
        models = root / "Sources/Models.swift"
        original_models = models.read_bytes()
        models.write_bytes(original_models.replace(b'case english', b'case renamedEnglish', 1))
        assert any("Models.swift" in failure for failure in check(root, baseline)), "Existing stored language values remain guarded"
        models.write_bytes(original_models)
        catalog = root / "Sources/LocalizationCatalog.swift"
        original = catalog.read_text()
        catalog.write_text(original.replace(baseline["translations"][0], "", 1))
        assert any("translated text" in failure for failure in check(root, baseline)), "Removed stable copy must fail"
        catalog.write_text(original + '\nEntry("Additional text", "新增", "新增", "追加"),\n')
        assert not check(root, baseline), "Additional integration strings may coexist with all stable translations"
        for name in baseline.get("reviewedBehaviorUpdates", {}):
            source = root / name
            original = source.read_bytes()
            source.write_bytes(original + b"\n// unreviewed mutation\n")
            assert any(name in failure for failure in check(root, baseline)), "Reviewed behavior changes remain hash guarded"
            source.write_bytes(original)
        catalog_original = catalog.read_text()
        for update in baseline.get("reviewedTranslationUpdates", {}).values():
            catalog.write_text(catalog_original.replace(update["entry"], "", 1))
            assert any("translated text" in failure for failure in check(root, baseline)), "Each reviewed replacement remains required"
            catalog.write_text(catalog_original)
        unauthorized = json.loads(json.dumps(baseline))
        protected = "Sources/UserProfileStore.swift"
        unauthorized["reviewedBehaviorUpdates"][protected] = {
            "baselineSha256": baseline["files"][protected], "sha256": baseline["files"][protected], "reason": "Test exemption"}
        assert any("Invalid reviewed behavior update" in failure for failure in check(root, unauthorized)), "Persistence files cannot be exempted"
        unauthorized = json.loads(json.dumps(baseline))
        original = next(iter(unauthorized["reviewedTranslationUpdates"]))
        unauthorized["reviewedTranslationUpdates"][original]["entry"] = 'Entry("Arbitrary", "任意", "任意", "任意")'
        assert any("Invalid reviewed translation update" in failure for failure in check(root, unauthorized)), "Unreviewed copy replacements cannot be exempted"
    count = 11 + len(baseline.get("reviewedBehaviorUpdates", {})) + len(baseline.get("reviewedTranslationUpdates", {}))
    print(f"Passed {count} isolated compatibility-guard mutation checks.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true", help="Verify guard failures in temporary copies")
    parser.add_argument("--behavioral", action="store_true", help="Also compile/run the existing isolated core tests")
    args = parser.parse_args()
    baseline = json.loads(MANIFEST.read_text())
    failures = check(ROOT, baseline)
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        print("Review against the stable baseline; do not regenerate hashes to bypass a regression.")
        return 1
    print(f"Stable {baseline['baselineCommit'][:7]} contract preserved: "
          f"{len(baseline['files'])} guarded functional/localization files "
          f"({len(baseline.get('reviewedBehaviorUpdates', {}))} explicitly reviewed behavior updates), "
          f"{len(baseline['translations'])} translated entries "
          f"({len(baseline.get('reviewedTranslationUpdates', {}))} exact reviewed replacements), and bundle/update identity.")
    if args.self_test:
        self_test(baseline)
    if args.behavioral:
        return subprocess.call(["bash", str(ROOT / "scripts/test.sh")], cwd=ROOT)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
