#!/usr/bin/env python3
"""Run the unchanged Foundation name expression on synthetic strings only."""
from pathlib import Path
import hashlib,json,subprocess
root=Path(__file__).resolve().parents[2]
out=root/'build/app-shortcut-reference';out.mkdir(parents=True,exist_ok=True)
source=(root/'Sources/HUDAppShortcutInteraction.swift').read_text()
expression='String(editor.string.components(separatedBy: .newlines).joined(separator: " ").prefix(128))'
assert 'let bounded = '+expression in source
values=['','Example','  keep spaces  ','A\r\nB\nC\rD\vE\fF\u0085G\u2028H\u2029I','A\tB','a'*128,'a'*129]
for cluster in ['e\u0301','👩🏽\u200d💻','🇰🇷','한','\U0001f469\u200d\U0001f469\u200d\U0001f467\u200d\U0001f466']:
    values.extend([cluster*128,cluster*129,'x'*127+cluster+'tail'])
values+=['x'*127+'\r\nend','x'*127+'e\u0301\u0301\u0301'+'end','中日한국어','','a'+'\u0301'*600]
(out/'field-input.json').write_text(json.dumps(values,ensure_ascii=False))
swift='''import Foundation
struct Editor { let string:String }
let input=try JSONSerialization.jsonObject(with:Data(contentsOf:URL(fileURLWithPath:CommandLine.arguments[1]))) as! [String]
let rows=input.map { value -> [String:Any] in
 let editor=Editor(string:value)
 let bounded = '''+expression+'''
 return ["input":value,"normalized":bounded,"utf16":bounded.utf16.count,"characters":bounded.count]
}
try JSONSerialization.data(withJSONObject:rows,options:[.sortedKeys]).write(to:URL(fileURLWithPath:CommandLine.arguments[2]))
'''
(out/'field.swift').write_text(swift)
subprocess.run(['xcrun','swiftc','-O','-sdk','/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk','-module-cache-path',str(root/'build/windows-module-reference/.compiler/module-cache'),str(out/'field.swift'),'-o',str(out/'field-reference')],check=True)
subprocess.run([str(out/'field-reference'),str(out/'field-input.json'),str(out/'field-rows.json')],check=True)
fixture={'version':1,'sourceSHA256':hashlib.sha256(source.encode()).hexdigest(),'expression':expression,'usesAppOrWindow':False,'rows':json.loads((out/'field-rows.json').read_text())}
target=root/'windows/tests/fixtures/app-shortcut-field-source.json'
target.write_text(json.dumps(fixture,ensure_ascii=False,separators=(',',':'))+'\n')
print(target)
