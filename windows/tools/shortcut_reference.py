#!/usr/bin/env python3
"""Run only the original shortcut decoding/name logic against synthetic values."""
import hashlib
import json
import os
import pathlib
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parents[2]
out = pathlib.Path(sys.argv[1]).resolve()
out.mkdir(parents=True, exist_ok=True)
target = out / 'shortcut-source.json'
if target.exists():
    raise SystemExit('Refusing to replace an existing source oracle')
source = root / 'Sources/AppShortcutStore.swift'
subprocess.run(['git', 'diff', '--quiet', 'ca04f142185c7de40acd8523bdb563195d90a1d1', '--', str(source)], cwd=root, check=True)
text = source.read_text()

def declaration(marker):
    start = text.index(marker)
    begin = text.index('{', start)
    depth = 1
    at = begin + 1
    # Selected declarations contain no brace literals, so no lexical rewrite is
    # needed. Keep the extracted bytes available in the compiler input for audit.
    while depth:
        depth += (text[at] == '{') - (text[at] == '}')
        at += 1
    return text[start:at]

parts = [declaration(m) for m in ['enum AppShortcutIcon:', 'struct AppShortcut:',
    'private struct Archive:', 'private struct Version:',
    'private static func validName(', 'private static func decode(']]
script = '''import Foundation
enum L10n { static func text(_ en:String,_ zh:String)->String { en } }
enum AppShortcutStoreError:Error { case invalidRecord,newerVersion }
'''+parts[0]+'\n'+parts[1]+'''\nenum AppShortcutStore {
'''+ '\n'.join(p.replace('private ', '') for p in parts[2:])+'''\n}
let names = ["", " ", "云终末地", "한국어", "é", "e\\u{301}", String(repeating:"e\\u{301}",count:128),String(repeating:"é",count:129),"🙂", "👩‍💻", "a\\n", "a\\r", "a\\t", "a\\0", "\\u{85}", "a\\u{200b}", "a\\u{2028}", "a\\u{2029}", "a\\u{2066}", "a\\u{feff}", "a\\u{e0001}", " \\n云终末地\\u{3000}"]
let row:[String:Any] = ["id":"abcdef00-0000-4000-8000-000000000001","name":"云终末地","originalName":"云终末地","iconPreset":"original","createdAt":812345678.125,"bookmark":"AQIDBA==","securityScoped":true,"lastKnownPath":"/Synthetic/云终末地.app"]
var records:[[String:Any]] = []
func test(_ name:String,_ value:[String:Any]) { let data=try! JSONSerialization.data(withJSONObject:value,options:.sortedKeys);var result="accepted";do{_ = try AppShortcutStore.decode(data)}catch AppShortcutStoreError.newerVersion{result="newerVersion"}catch{result="invalidRecord"};records.append(["name":name,"archive":value,"result":result]) }
test("mac",["version":1,"items":[row]])
test("empty",["version":1,"items":[]])
test("newer",["version":2,"items":[]])
test("zero",["version":0,"items":[]])
for (key,value) in [("name", ""),("name","a\\n"),("originalName",""),("iconPreset","future"),("bookmark",""),("lastKnownPath","relative.app"),("lastKnownPath","/Synthetic/app.exe"),("lastKnownPath","/Synthetic/App.APP")] {var next=row;next[key]=value;test(key+"="+value,["version":1,"items":[next]])}
var duplicate=row;duplicate["id"]="ABCDEF00-0000-4000-8000-000000000001";test("duplicateUUIDCase",["version":1,"items":[row,duplicate]])
let output:[String:Any] = ["names":names.map{["text":$0,"valid":AppShortcutStore.validName($0),"trimmed":$0.trimmingCharacters(in:.whitespacesAndNewlines)]},"icons":AppShortcutIcon.allCases.map(\\.rawValue),"records":records]
let data=try! JSONSerialization.data(withJSONObject:output,options:[.sortedKeys]);try! data.write(to:URL(fileURLWithPath:CommandLine.arguments[1]))
'''
compiler = out / '.compiler'
compiler.mkdir(exist_ok=True)
(compiler / 'main.swift').write_text(script)
env = os.environ.copy()
env.pop('SDKROOT', None)
sdk = subprocess.check_output(['bash', str(root / 'scripts/build.sh'), '--print-sdk'], text=True, env=env).strip()
subprocess.run(['xcrun', 'swiftc', '-O', '-sdk', sdk, '-module-cache-path', str(root / 'build/windows-module-reference/.compiler/module-cache'), str(compiler / 'main.swift'), '-o', str(compiler / 'reference')], check=True, env=env)
subprocess.run([str(compiler / 'reference'), str(target)], check=True)
data = json.loads(target.read_text())
data['sourceAuthority'] = 'ca04f142185c7de40acd8523bdb563195d90a1d1'
data['sourceSHA256'] = hashlib.sha256(source.read_bytes()).hexdigest()
data['extractedDeclarationsSHA256'] = [hashlib.sha256(p.encode()).hexdigest() for p in parts]
data['isolation'] = 'Original pure decode/name/icon declarations; synthetic records only; no store, file inspection or launch.'
target.write_text(json.dumps(data, ensure_ascii=False, separators=(',', ':'))+'\n')
print(target)
