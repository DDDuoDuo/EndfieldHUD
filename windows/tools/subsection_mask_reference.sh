#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
if [[ $# -ne 2 || "$(uname -s)" != Darwin ]]; then
    echo "Usage on macOS: subsection_mask_reference.sh NEW_OUTPUT EXISTING_MODULE_CACHE" >&2
    exit 1
fi
OUTPUT="$1"; CACHE="$2"
[[ ! -e "$OUTPUT" && ! -L "$OUTPUT" && -d "$CACHE" ]]
mkdir -p "$OUTPUT"
OUTPUT="$(cd "$OUTPUT" && pwd)"; CACHE="$(cd "$CACHE" && pwd)"
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk)"
python3 - "$ROOT" "$OUTPUT" "$SDK" <<'PY'
import hashlib,json,pathlib,subprocess,sys
root,out=map(pathlib.Path,sys.argv[1:3]); source=root/'Sources/HUDSubsectionTransition.swift'
expected='c73bfac1c87385dafb7c9387c8d834c937e37eb51e189ad6425602dd6fcff355'
actual=hashlib.sha256(source.read_bytes()).hexdigest()
if actual!=expected:raise SystemExit('Pinned original subsection source changed; review before regenerating')
(out/'provenance.json').write_text(json.dumps({'source':'Sources/HUDSubsectionTransition.swift','sourceSHA256':actual,
 'toolSHA256':hashlib.sha256((root/'windows/tools/subsection_mask_reference.swift').read_bytes()).hexdigest(),
 'scope':'Canonical shelf viewport only, own paused offscreen CA layers, no real data/capture/providers/runtime integration',
 'macOS':subprocess.check_output(['sw_vers','-productVersion'],text=True).strip(),
 'osBuild':subprocess.check_output(['sw_vers','-buildVersion'],text=True).strip(),'sdk':sys.argv[3]},indent=2)+'\n')
PY
xcrun swiftc -swift-version 5 -O -parse-as-library -sdk "$SDK" -module-cache-path "$CACHE" \
    -framework AppKit -framework QuartzCore "$ROOT/Sources/HUDSubsectionTransition.swift" \
    "$ROOT/windows/tools/subsection_mask_reference.swift" -o "$OUTPUT/subsection-mask-reference" >"$OUTPUT/compile.log" 2>&1
python3 - "$ROOT" "$OUTPUT" <<'PY'
import hashlib,json,math,os,pathlib,struct,subprocess,sys,tempfile
root,out=map(pathlib.Path,sys.argv[1:3])
with tempfile.TemporaryDirectory(prefix='endfield-subsection-mask-') as fixture:
    subprocess.run([str(out/'subsection-mask-reference'),str(out)],env=dict(os.environ,CFFIXED_USER_HOME=fixture),check=True)
provenance=json.loads((out/'provenance.json').read_text())
if hashlib.sha256((root/'Sources/HUDSubsectionTransition.swift').read_bytes()).hexdigest()!=provenance['sourceSHA256']:
    raise SystemExit('Original source changed during oracle run')
binary=out/'candidate.bin'
if binary.exists():
    data=binary.read_bytes();position=0;assert len(data)<256*1024 and data[:8]==b'EHSUBC01';position=8
    def read(fmt):
        global position
        size=struct.calcsize('<'+fmt);assert position+size<=len(data)
        result=struct.unpack_from('<'+fmt,data,position);position+=size
        return result[0] if len(result)==1 else result
    assert read('5d')==(9.0,40.0,382.0,248.0,0.26) and read('I')==2
    maximum_ops=maximum_coordinates=maximum_subpaths=fit_count=0
    decoded=[]
    for expected in [-1,1]:
        assert read('i')==expected
        topology_count=read('I');assert 1<=topology_count<=8;topologies=[]
        for _ in range(topology_count):
            count=read('I');assert 1<=count<=40;codes=read(str(count)+'B');assert all(c in range(5) for c in codes)
            assert codes[0]==0 and 4<=codes.count(0)<=8 and codes.count(0)==codes.count(4)
            maximum_subpaths=max(maximum_subpaths,codes.count(0))
            topologies.append(codes);maximum_ops=max(maximum_ops,count)
        count=read('I');assert 1<=count<=128;pieces=[]
        for _ in range(count):
            lo,hi=read('2d');assert math.isfinite(lo) and math.isfinite(hi) and 0<=lo<hi<=1
            topology,samples,coordinates=read('3I');assert topology<topology_count and samples in (1,4)
            assert coordinates==sum((2,2,4,6,0)[c] for c in topologies[topology]) and coordinates<=192
            values=read(str(samples*coordinates)+'f');assert all(math.isfinite(v) for v in values)
            pieces.append((lo,hi,'fit'));maximum_coordinates=max(maximum_coordinates,coordinates);fit_count+=1
        jumps=read('I');assert jumps<=12
        for _ in range(jumps):
            lo,hi=read('2d');assert 0<=lo<hi<=1 and hi-lo<=1e-12;pieces.append((lo,hi,'unresolved switch'))
        pieces.sort();assert pieces[0][0]==0 and pieces[-1][1]==1
        assert all(a[1]==b[0] for a,b in zip(pieces,pieces[1:]));decoded.append({'direction':expected,'intervals':len(pieces),'switches':jumps})
    assert position==len(data)
    report=json.loads((out/'report.json').read_text());report['candidateSHA256']=hashlib.sha256(data).hexdigest()
    report['independentBinaryValidation']={'bytesConsumed':position,'fitCount':fit_count,'maximumOpcodes':maximum_ops,'maximumCoordinateScalars':maximum_coordinates,'maximumNormalizedSubpaths':maximum_subpaths,'completeCoverageWithExplicitGaps':decoded}
    (out/'report.json').write_text(json.dumps(report,indent=2,sort_keys=True)+'\n')
PY
