#!/bin/bash
set -euo pipefail

# Inspect and expand a package; never run Installer or change installed apps.
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd -P)"
if [ "$(uname -s)" != "Darwin" ] || [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
    printf 'Usage on macOS: %s PACKAGE_PATH EXPECTED_APP_PATH [REPORT_DIRECTORY]\n' "$0" >&2
    exit 2
fi
PACKAGE="$(cd "$(dirname "$1")" && pwd -P)/$(basename "$1")"
APP="$(cd "$2" && pwd -P)"
REPORT_DIRECTORY="${3:-$PROJECT_DIR/build/installer-verification}"
REPORT_DIRECTORY="$(python3 -c 'from pathlib import Path; import sys; print(Path(sys.argv[1]).resolve())' "$REPORT_DIRECTORY")"
case "$REPORT_DIRECTORY/" in "$APP/"*) printf 'Reports must be outside the app bundle.\n' >&2; exit 2 ;; esac
mkdir -p "$REPORT_DIRECTORY"
VERIFY_STAGE="$(mktemp -d "${TMPDIR:-/tmp}/EndfieldHUD-installer-check.XXXXXX")"
trap 'rm -rf "$VERIFY_STAGE"' EXIT
pkgutil --expand-full "$PACKAGE" "$VERIFY_STAGE/expanded"
python3 - "$VERIFY_STAGE/expanded" "$APP" "$REPORT_DIRECTORY" "$PROJECT_DIR" <<'PY'
import hashlib
import json
import os
from pathlib import Path
import plistlib
import stat
import subprocess
import sys
import xml.etree.ElementTree as ET

expanded, original, reports, project = map(Path, sys.argv[1:])
with (original / "Contents/Info.plist").open("rb") as file:
    expected_info = plistlib.load(file)
identifier, build = expected_info["CFBundleIdentifier"], expected_info["CFBundleVersion"]
assert identifier == "io.github.endfieldcharge.EndfieldCharge", "Unexpected app identifier"
distribution = ET.parse(expanded / "Distribution").getroot()
assert distribution.tag == "installer-gui-script"
assert not distribution.findall(".//script"), "Distribution scripts are forbidden"
assert not distribution.findall(".//relocate"), "Installer must not relocate another copy"
options = distribution.find("options")
assert options is not None and options.get("require-scripts") == "false"
assert options.get("allow-external-scripts") == "false"
assert options.get("rootVolumeOnly") == "true"
assert set(options.get("hostArchitectures", "").split(",")) == {"arm64", "x86_64"}
domains = distribution.find("domains")
assert domains is not None and domains.attrib == {"enable_localSystem": "true", "enable_currentUserHome": "false", "enable_anywhere": "false"}
references = distribution.findall(".//pkg-ref")
assert references and all(reference.get("id") == identifier for reference in references)
assert all(reference.get("onConclusion", "None") == "None" for reference in references), "No logout/restart is permitted"
assert all("onConclusionScript" not in reference.attrib for reference in references)
assert [node.get("id") for node in distribution.findall(".//must-close/app")] == [identifier]
assert distribution.find("product").get("version") == build
components = list(expanded.glob("*.pkg"))
assert len(components) == 1, "Expected exactly one payload package"
component = components[0]
assert set(path.name for path in component.iterdir()) <= {"PackageInfo", "Payload", "Bom"}, "Unexpected scripts or package component"
package_info = ET.parse(component / "PackageInfo").getroot()
assert package_info.get("identifier") == identifier and package_info.get("version") == build
assert package_info.get("install-location") == "/"
assert package_info.get("postinstall-action", "none") == "none", "No logout/restart is permitted"
assert package_info.find("scripts") is None, "Installer scripts are forbidden"
assert package_info.get("relocatable") == "false", "Component must not be relocatable"
assert not package_info.findall("relocate/*"), "Component must not search for another installed copy"
bundle = package_info.find("bundle")
assert bundle is not None and bundle.get("id") == identifier
assert Path(bundle.get("path", "")) == Path("Applications/EndfieldHUD.app")
assert bundle.get("CFBundleVersion") == build
assert package_info.find("upgrade-bundle/bundle").get("id") == identifier, "Bundle replacement must use upgrade semantics"
assert package_info.find("strict-identifier/bundle").get("id") == identifier
assert package_info.find("bundle-version/bundle").get("id") == identifier
payload = component / "Payload"
assert set(path.name for path in payload.iterdir()) == {"Applications"}
assert set(path.name for path in (payload / "Applications").iterdir()) == {"EndfieldHUD.app"}
packaged_app = payload / "Applications/EndfieldHUD.app"


def manifest(root, normalized_modes=False):
    entries = {".": ["directory", 0o755 if normalized_modes else stat.S_IMODE(root.stat().st_mode)]}
    for folder, directories, files in os.walk(root, followlinks=False):
        for name in directories + files:
            path = Path(folder) / name
            relative = str(path.relative_to(root))
            mode = path.lstat().st_mode
            if stat.S_ISLNK(mode):
                target = os.readlink(path)
                assert root.resolve() in path.resolve().parents, f"Symlink escapes bundle: {relative}"
                entries[relative] = ["link", target]
            elif stat.S_ISDIR(mode):
                entries[relative] = ["directory", 0o755 if normalized_modes else stat.S_IMODE(mode)]
            elif stat.S_ISREG(mode):
                expected_mode = (0o755 if mode & 0o111 else 0o644) if normalized_modes else stat.S_IMODE(mode)
                entries[relative] = ["file", expected_mode, hashlib.sha256(path.read_bytes()).hexdigest()]
            else:
                raise AssertionError(f"Unexpected special file: {relative}")
    return entries


original_manifest, packaged_manifest = manifest(original, normalized_modes=True), manifest(packaged_app)
assert original_manifest == packaged_manifest, "Packaged app bytes or links changed, or permissions are not standard distributable modes"
subprocess.run([sys.executable, str(project / "scripts/normalize-bundle-permissions.py"), "--check", str(packaged_app)], check=True)
verification = subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict", "--all-architectures", "--verbose=2", str(packaged_app)],
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=30)
(reports / "payload-signature.log").write_text(verification.stdout)
assert verification.returncode == 0, "Expanded app signature did not verify"
summary = {"passed": True, "bundle_identifier": identifier, "release_version": expected_info["CFBundleShortVersionString"],
           "build": build, "package_version": package_info.get("version"), "payload": "/Applications/EndfieldHUD.app",
           "scripts": False, "relocatable": False, "overwrite_action": "upgrade", "must_close": identifier,
           "unchanged_content_entries": len(original_manifest), "standard_permissions": True, "signature_verified": True}
(reports / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
print(json.dumps(summary, indent=2))
PY
