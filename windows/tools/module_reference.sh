#!/bin/bash
set -euo pipefail
# Isolated actual-source CALayer export. Usage: module_reference.sh [output-dir]
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ "$(uname -s)" == Darwin ]] || { echo "macOS is required for source canvas export." >&2; exit 1; }
OUTPUT="${1:-$ROOT/build/windows-module-reference}"
[[ $# -le 1 ]] || { echo "Usage: module_reference.sh [output-dir]" >&2; exit 1; }
mkdir -p "$OUTPUT"
OUTPUT="$(cd "$OUTPUT" && pwd)"
BUILD="$OUTPUT/.compiler"
mkdir -p "$BUILD/module-cache"
FIXTURE_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/endfield-module-reference.XXXXXX")"
trap 'rm -rf "$FIXTURE_ROOT"' EXIT
cd "$ROOT"
python3 - "$ROOT" "$OUTPUT" <<'PY'
import hashlib, json, pathlib, re, subprocess, sys
root, output = map(pathlib.Path, sys.argv[1:])
source = root/'Sources/SystemHUDView.swift'
text = source.read_text()
def block(start, end):
    return text.split(start, 1)[1].split(end, 1)[0].strip()
scale = block('private var reportScale: CGFloat {', 'private var reportCenterY: CGFloat {')
center = block('private var reportCenterY: CGFloat {', 'private func reportRect(')
rect = block('private func reportRect(_ rect: CGRect) -> CGRect {', 'private func centerPoint(')
notes_project = block('private func projectNotesRect(_ rect: CGRect) -> CGRect {', 'private func notesWorkspacePoint(')
notes_layout = text.split('self.designScale = max(0.1, min(', 1)[1].split('// Screen-sized notes', 1)[0]
assert 'self.bounds.width / 1100, self.bounds.height / 740, 1.15)) * CGFloat(self.configuration.hudScale)' in notes_layout
assert '(self.bounds.width - 1000 * self.designScale) / 2' in notes_layout
assert '(self.bounds.height - 640 * self.designScale) / 2 - Self.verticalLift * self.designScale' in notes_layout
assert 'private static let verticalLift: CGFloat = 30' in text
assert 'designRect = CGRect(origin: designPoint(rect.origin)' in notes_project
assert 'rect.width / designScale, height: rect.height / designScale' in notes_project
# Fail closed if these source functions change. This table describes only the
# six exported modules and is explicitly not a full SystemHUDView runtime probe.
assert 'return selectedModule == .workMode ? 1 : 0.86' in scale
assert 'guard selectedModule == .workMode || selectedModule == .map else { return usesSourceShell ? 285 : 294 }' in center
assert '500 + (rect.minX - 500) * reportScale' in rect
assert 'reportCenterY + (rect.minY - 320) * reportScale' in rect
assert not set(re.findall(r'\.([A-Za-z][A-Za-z0-9]*)', scale + center)) & {'notes','profile','system','display','hotkeys','about'}
attachment = {
    'evidence': 'source-derived declarative formulas, not full SystemHUDView runtime placement',
    'sourcePath': 'Sources/SystemHUDView.swift', 'sourceSHA256': hashlib.sha256(source.read_bytes()).hexdigest(),
    'sourceExcerpts': {'reportScale': scale, 'reportCenterY': center, 'reportRect': rect, 'projectNotesRect': notes_project,
                       'notesLayout': 'self.designScale = max(0.1, min(' + notes_layout},
    'exportedModules': ['notes','profile','system','display','hotkeys','about'],
    'desktopMode': True, 'reportScale': 0.86, 'reportCenter': [500,285], 'designCenter': [500,320],
    'moduleLocalToDesign': 'add contentFrame origin',
    'designToReport': 'x=500+(x-500)*0.86; y=285+(y-320)*0.86',
    'reportToScreen': 'apply actual desktopCenterProjection homogeneous matrix',
    'workspaceException': 'Notes workspace stores overlay screen units but is still projected. Do not apply reportScale/reportCenterY. Convert to design coordinates, then apply actual desktopCenterProjection.',
    'notesWorkspace': {'storedViewport':[1920,1080], 'designScale':'max(0.1,min(width/1100,height/740,1.15))*hudScale',
        'designOrigin':'[(width-1000*scale)/2+width*hudOffsetX, (height-640*scale)/2-30*scale+height*hudOffsetY]',
        'workspaceToDesign':'(point-designOrigin)/designScale', 'designToScreen':'actual desktopCenterProjection',
        'fixtureHUDScale':1, 'fixtureHUDOffset':[0,0], 'verticalLift':30},
    'fullSystemHUDViewPlacementVerified': False,
}
(output/'attachment.json').write_text(json.dumps(attachment, indent=2, sort_keys=True)+'\n')
files = sorted(p for folder in ('Sources','Resources') for p in (root/folder).rglob('*') if p.is_file())
manifest = {str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
provenance = {'baselineRequested':'ca04f14', 'gitHead':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
              'sourceAndResourcesMatchBaseline':subprocess.run(['git','diff','--quiet','ca04f14','--','Sources','Resources']).returncode == 0,
              'sourceAndResourceSHA256':manifest,
              'exporterSHA256':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((root/'windows/tools').glob('module_reference*'))},
              'compiler':subprocess.check_output(['xcrun','swiftc','--version'],text=True).strip()}
(output/'provenance.json').write_text(json.dumps(provenance, indent=2, sort_keys=True)+'\n')
PY
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk)"
SOURCES=()
while IFS= read -r path; do SOURCES+=("$ROOT/$path"); done < <(rg --files Sources -g '*.swift' | sed '/\/main.swift$/d' | LC_ALL=C sort)
if ! xcrun swiftc -swift-version 5 -Onone -whole-module-optimization -parse-as-library \
    -module-name EndfieldModuleReference -D HUD_WATCH_MOTION_PREVIEW \
    -sdk "$SDK" -module-cache-path "$BUILD/module-cache" \
    -framework Cocoa -framework IOKit -framework CoreAudio -framework Quartz \
    -framework Carbon -framework ServiceManagement -framework Metal -framework MetalKit \
    -framework WebKit -framework Security -framework PDFKit -framework JavaScriptCore -lsqlite3 -lz \
    "${SOURCES[@]}" "$ROOT/windows/tools/module_reference_layers.swift" "$ROOT/windows/tools/module_reference.swift" \
    -o "$BUILD/module-reference" >"$BUILD/compile.log" 2>&1; then
    tail -100 "$BUILD/compile.log" >&2
    exit 1
fi
CFFIXED_USER_HOME="$FIXTURE_ROOT" "$BUILD/module-reference" --ui-test --render-module-reference --output "$OUTPUT"
python3 "$ROOT/windows/tests/test_module_reference.py" "$OUTPUT" --source-root "$ROOT"
