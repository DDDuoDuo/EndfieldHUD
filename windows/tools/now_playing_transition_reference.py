#!/usr/bin/env python3
"""Measure original CATransition on an owned detached8px Metal target."""
import hashlib,json,subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parents[2];out=root/'build/now-playing-transition-reference';out.mkdir(exist_ok=True)
authority='ca04f142185c7de40acd8523bdb563195d90a1d1';source='Sources/NowPlayingCanvas.swift'
raw=(root/source).read_bytes();assert raw==subprocess.check_output(['git','show',authority+':'+source],cwd=root)
text=raw.decode();begin=text.index('    private func crossfade(');end=text.index('\n    private func removeAnimations(',begin)
helper='import QuartzCore\nlet active=true,rendered=true\nfunc reduceMotion()->Bool {false}\n'+text[begin:end]+'\n'
# Same-declaration frozen-clock measurement; the original guard/body executes.
(out/'source-crossfade.swift').write_text(helper)
tool=root/'windows/tools/now_playing_transition_reference.swift';(out/'main.swift').write_text(helper+tool.read_text())
subprocess.run(['/usr/bin/swiftc','-O','-sdk','/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk','-module-cache-path',str(root/'build/windows-module-reference/.compiler/module-cache'),'-framework','QuartzCore','-framework','Metal','-framework','CoreGraphics',str(out/'main.swift'),'-o',str(out/'source-oracle')],check=True)
subprocess.run([str(out/'source-oracle'),str(out/'source-raw.json')],check=True)
value=json.loads((out/'source-raw.json').read_text());value.update({'sourceCommit':authority,'sourcePins':{source:hashlib.sha256(raw).hexdigest()},'scope':'Unchanged original crossfade method with active/rendered and reduced-motion false; detached8px CARenderer with frozen clock. RGBA blending/interruption evidence, not whole view/font pixel parity.','toolSHA256':hashlib.sha256(tool.read_bytes()).hexdigest()})
destination=Path(sys.argv[1]) if len(sys.argv)>1 else out/'source.json'
if destination.exists():raise RuntimeError('Choose a new immutable output path')
destination.write_text(json.dumps(value,separators=(',',':'))+'\n');print(destination,len(destination.read_bytes()),hashlib.sha256(destination.read_bytes()).hexdigest())
