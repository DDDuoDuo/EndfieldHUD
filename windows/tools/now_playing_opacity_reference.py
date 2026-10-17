#!/usr/bin/env python3
"""Owned8px source hierarchy/opacity probe; no app, player, font or window."""
import hashlib,json,subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parents[2];out=root/'build/now-playing-scene-reference';out.mkdir(exist_ok=True)
authority='ca04f142185c7de40acd8523bdb563195d90a1d1'
paths=['Sources/NowPlayingCanvas.swift','Sources/HUDModuleContent.swift'];pins={}
for path in paths:
 raw=(root/path).read_bytes();assert raw==subprocess.check_output(['git','show',authority+':'+path],cwd=root);pins[path]=hashlib.sha256(raw).hexdigest()
source=(root/paths[0]).read_text();assert 'content.allowsGroupOpacity = false' in source
assert 'wrapper.allowsGroupOpacity = false' in (root/paths[1]).read_text()
begin=source.index('        if changed, active, !reduceMotion() {',source.index('    private func renderVolume()'))
end=source.index('\n        guard showVolume',begin)
helper='import QuartzCore\nprivate func sourceVolumeFade(_ volumeMenu:CALayer,_ previous:Float) {\nlet changed=true,active=true\nlet destination:Float=1\nfunc reduceMotion()->Bool {false}\n'+source[begin:end]+'\n}\n'
tool=root/'windows/tools/now_playing_opacity_reference.swift';(out/'main-opacity.swift').write_text(helper+tool.read_text())
# Top-level Swift entry must be named main.swift; do not overwrite any other
# source reference's generated entry or module cache.
compile_dir=out/'opacity-source';compile_dir.mkdir(exist_ok=True);(compile_dir/'main.swift').write_text(helper+tool.read_text())
subprocess.run(['/usr/bin/swiftc','-O','-sdk','/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk','-module-cache-path',str(root/'build/windows-module-reference/.compiler/module-cache'),'-framework','QuartzCore','-framework','Metal','-framework','CoreGraphics',str(compile_dir/'main.swift'),'-o',str(compile_dir/'oracle')],check=True)
subprocess.run([str(compile_dir/'oracle'),str(compile_dir/'raw.json')],check=True)
value=json.loads((compile_dir/'raw.json').read_text());value.update({'sourceCommit':authority,'sourcePins':pins,'scope':'Original wrapper/content group-opacity flags and unchanged original volume fade block; two synthetic overlapping leaves on detached8px CARenderer. Includes source volume backing/face palette interiors. No whole-view/font pixel parity.','toolSHA256':hashlib.sha256(tool.read_bytes()).hexdigest()})
destination=Path(sys.argv[1]) if len(sys.argv)>1 else out/'opacity-source.json'
if destination.exists():raise RuntimeError('Choose a fresh immutable output')
destination.write_text(json.dumps(value,separators=(',',':'))+'\n');print(destination,len(destination.read_bytes()),hashlib.sha256(destination.read_bytes()).hexdigest())
