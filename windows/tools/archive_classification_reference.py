#!/usr/bin/env python3
"""Mac oracle for classified Archive documents: compiles the unchanged
Sources/ArchiveClassification.swift (1.3.0) with a stub ArchiveError and runs
ArchivePassword.issue, ArchivePasswordVerifier.derive/encoded/init?(data:) and
ArchivePassword.account over synthetic inputs. No Keychain item is created,
read or removed (only ArchiveMemoryPasswordVault is exercised); no app data.

Usage: archive_classification_reference.py [BUILD_DIR]
Writes windows/tests/fixtures/archive-classification-source.json.
"""
from pathlib import Path
import hashlib, json, os, subprocess, sys

root = Path(__file__).resolve().parents[2]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / 'build/archive-classification-reference'
build.mkdir(parents=True, exist_ok=True)
source = root / 'Sources/ArchiveClassification.swift'
# Passwords as Swift strings (JSON escapes). NFD and NFC spell the same password.
passwords = ['correct horse', 'pásswörd', 'pásswörd', '档案密码',
             '\U0001F469\U0001F3FD‍\U0001F4BB\U0001F680\U0001F680\U0001F680', 'x' * 1024]
issues = [('abc', None), ('abcd', None), ('\U0001F469\U0001F3FD‍\U0001F4BB' * 3, None), ('\U0001F469\U0001F3FD‍\U0001F4BB' * 4, None),
          ('ééé', None), ('éééé', None), ('x' * 1024, None), ('x' * 1025, None),
          ('é' * 600, None), ('abc', 'abd'), ('abcd', 'abce'), ('abcd', 'abcd'), ('', '')]
records = [
    '{"algorithm":"PBKDF2-HMAC-SHA256","iterations":600000,"key":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=","salt":"AAAAAAAAAAAAAAAAAAAAAA==","version":1}',
    '{"algorithm":"PBKDF2-HMAC-SHA256","iterations":599999,"key":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=","salt":"AAAAAAAAAAAAAAAAAAAAAA==","version":1}',
    '{"algorithm":"PBKDF2-HMAC-SHA256","iterations":10000001,"key":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=","salt":"AAAAAAAAAAAAAAAAAAAAAA==","version":1}',
    '{"algorithm":"PBKDF2-HMAC-SHA256","iterations":10000000,"key":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=","salt":"AAAAAAAAAAAAAAAAAAAAAA==","version":2}',
    '{"algorithm":"PBKDF2-HMAC-SHA1","iterations":600000,"key":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=","salt":"AAAAAAAAAAAAAAAAAAAAAA==","version":1}',
    '{"algorithm":"PBKDF2-HMAC-SHA256","iterations":600000,"key":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA==","salt":"AAAAAAAAAAAAAAAAAAAAAA==","version":1}',
    '{"algorithm":"PBKDF2-HMAC-SHA256","iterations":600000,"key":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=","salt":"AAAAAAAAAAAAAAAAAAAA","version":1}',
    '{"algorithm":"PBKDF2-HMAC-SHA256","iterations":600000,"key":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=","salt":"AAAAAAAAAAAAAAAAAAAAAA==","version":1,"future":true}',
    '{"algorithm":"PBKDF2-HMAC-SHA256","iterations":"600000","key":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=","salt":"AAAAAAAAAAAAAAAAAAAAAA==","version":1}',
    '{"algorithm":"PBKDF2-HMAC-SHA256","iterations":600000.0,"key":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=","salt":"AAAAAAAAAAAAAAAAAAAAAA==","version":1}',
    '{"algorithm":"PBKDF2-HMAC-SHA256","iterations":-1,"key":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=","salt":"AAAAAAAAAAAAAAAAAAAAAA==","version":1}',
    '{"algorithm":"PBKDF2-HMAC-SHA256","iterations":600000,"key":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=","salt":"AAAAAAAAAAAAAAAAAAAAAA==","version":1' + ' ' * 4000 + '}',
    '{"iterations":600000,"key":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=","salt":"AAAAAAAAAAAAAAAAAAAAAA==","version":1}',
    '{"algorithm":"PBKDF2-HMAC-SHA256","iterations":600000,"key":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=","salt":"AAAAAAAAAAAAAAAAAAAAAA==","version":1.0}',
    '{"algorithm":"PBKDF2-HMAC-SHA256","iterations":600000,"key":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAB=","salt":"AAAAAAAAAAAAAAAAAAAAAA==","version":1}',
    '{"algorithm":"PBKDF2-HMAC-SHA256","iterations":600000,"key":"AAAAAAAAAAAA AAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=","salt":"AAAAAAAAAAAAAAAAAAAAAA==","version":1}',
    'not json',
]
(build / 'input.json').write_text(json.dumps({'passwords': passwords, 'issues': [[p, c] for p, c in issues], 'records': records}))
(build / 'main.swift').write_text(r'''import Foundation
enum ArchiveError: Error { case invalid, unavailable }
let input = try JSONSerialization.jsonObject(with: Data(contentsOf: URL(fileURLWithPath: CommandLine.arguments[1]))) as! [String: Any]
let salt = Data((0..<16).map { UInt8($0 * 7 + 3) })
var derived: [[String: Any]] = []
for password in input["passwords"] as! [String] {
    let key = ArchivePasswordVerifier.derive(password, salt: salt, iterations: 600_000)!
    let verifier = ArchivePasswordVerifier(iterations: 600_000, salt: salt, key: key)
    precondition(verifier.matches(password))
    derived.append(["password": password, "issue": ArchivePassword.issue(password).map { "\($0)" } ?? NSNull(),
                    "record": String(decoding: try verifier.encoded(), as: UTF8.self)])
}
var issues: [[String: Any]] = []
for row in input["issues"] as! [[Any]] {
    let password = row[0] as! String, confirmation = row[1] as? String
    issues.append(["password": password, "confirmation": confirmation ?? NSNull(), "issue": ArchivePassword.issue(password, confirmation: confirmation).map { "\($0)" } ?? NSNull()])
}
var records: [[String: Any]] = []
for text in input["records"] as! [String] { records.append(["record": text, "accepted": ArchivePasswordVerifier(data: Data(text.utf8)) != nil]) }
let document = UUID(uuidString: "b0000000-0000-4000-8000-000000000001")!, key = UUID(uuidString: "c0000000-0000-4000-8000-0000000000aa")!
let vault = ArchiveMemoryPasswordVault()
try vault.add(Data("x".utf8), account: "a")
var duplicate = false
do { try vault.add(Data("y".utf8), account: "a") } catch { duplicate = true }
try vault.remove(account: "absent")
let output: [String: Any] = ["salt": salt.base64EncodedString(), "derived": derived, "issues": issues, "records": records,
    "account": ArchivePassword.account(document: document, key: key), "duplicateRejected": duplicate,
    "limits": ["minimumIterations": ArchivePasswordVerifier.minimumIterations, "maximumIterations": ArchivePasswordVerifier.maximumIterations,
               "minimumLength": ArchivePassword.minimumLength, "maximumBytes": ArchivePassword.maximumBytes]]
try JSONSerialization.data(withJSONObject: output, options: [.sortedKeys]).write(to: URL(fileURLWithPath: CommandLine.arguments[2]))
''')
sdk = subprocess.run(['bash', str(root / 'windows/tools/mac_sdk.sh')], capture_output=True, text=True, check=True).stdout.strip().splitlines()[-1]
cache = build / 'module-cache'; cache.mkdir(parents=True, exist_ok=True)
subprocess.run(['xcrun', 'swiftc', '-O', '-sdk', sdk, '-module-cache-path', str(cache), '-framework', 'Security', '-framework', 'LocalAuthentication',
                str(source), str(build / 'main.swift'), '-o', str(build / 'reference')], check=True, env=dict(os.environ, SDKROOT=sdk))
subprocess.run([str(build / 'reference'), str(build / 'input.json'), str(build / 'output.json')], check=True)
document = json.loads((build / 'output.json').read_text())
document['source'] = {'path': 'Sources/ArchiveClassification.swift', 'sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                      'generator': 'windows/tools/archive_classification_reference.py'}
fixture = root / 'windows/tests/fixtures/archive-classification-source.json'
fixture.write_text(json.dumps(document, ensure_ascii=False, indent=1, sort_keys=True) + '\n')
print(f"PASS {len(document['derived'])} derivations, {len(document['issues'])} password rules, {len(document['records'])} records: {fixture}")
