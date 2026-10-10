#!/usr/bin/env python3
"""Mac oracle for the offline data import: Apple's PropertyListSerialization
and the unchanged ConfigurationStore/OrbiPomSession over throwaway preference
suites inside a private CFFIXED_USER_HOME. Never reads the real app domain.

Usage: mac_import_reference.py [BUILD_DIR]
Writes windows/tests/fixtures/plist_mac_import_source.json,
windows/tests/fixtures/mac_import_settings_source.json and
windows/tests/fixtures/mac_import_golden_export.json (the unchanged Mac stores
write synthetic records into a private folder, then mac_import_exporter.py
exports them exactly as it would on a user's Mac).
"""
from pathlib import Path
import base64, hashlib, json, os, struct, subprocess, sys, tempfile

root = Path(__file__).resolve().parents[2]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / 'build/scratch-import-oracle'
build.mkdir(parents=True, exist_ok=True)

def real(x): return {'real': struct.pack('>d', x).hex()}
def real32(x): return {'real32': struct.pack('>d', x).hex()}
def date(x): return {'date': struct.pack('>d', x).hex()}
def i(x): return {'int': str(x)}
def s(x): return {'string': x}
def b(x): return {'bool': x}
def data(x): return {'data': base64.b64encode(x).decode()}
def arr(*x): return {'array': list(x)}
def d(**x): return {'dict': x}
def dd(x): return {'dict': x}
def shortcut(**x): return data(json.dumps(x, separators=(',', ':')).encode())
nan = float('nan'); inf = float('inf')

values = [
    ('integers', arr(i(0), i(1), i(127), i(128), i(255), i(256), i(65535), i(65536), i(2**31 - 1), i(2**32), i(2**53 + 1),
                     i(2**63 - 1), i(2**63), i(2**64 - 1), i(-1), i(-128), i(-2**31), i(-2**63))),
    ('reals', arr(real(0.0), real(-0.0), real(0.63), real(0.1), real(1e308), real(5e-324), real(-2.5), real(inf), real(-inf), real(nan),
                  real(1 / 3), real(123456.0), real32(0.5), real32(0.1))),
    ('dates', arr(date(0.0), date(-978307200.0), date(1234567.125), date(-1.5), date(800000000.0), date(63113904000.0))),
    ('strings', arr(s(''), s('plain ASCII'), s('<&>"\''), s('Doctor 博士 🚀'), s('é combining'), s('\U0001F468‍\U0001F469‍\U0001F467'),
                    s('tab\tnew\nline\r'), s('  '), s('x' * 300))),
    ('data', arr(data(b''), data(bytes(range(256))), data(b'\x00'))),
    ('nested', d(empty=dd({}), list=arr(), deep=arr(arr(arr(d(k=s('v'))))), flag=b(True), off=b(False),
                 unicodeKey=dd({'键🔑': i(7)}))),
    ('preferences', d(accentHex=s('FAD41F'), hudScale=real(1.25), launchAtLogin=b(False), customScreenID=i(4294967295),
                      summonShortcut=shortcut(keyCode=50, modifiers=1), lastSeen=date(700000000.5))),
    ('largeContainer', {'array': [i(n) for n in range(40)]}),
]

def raw(name, text, stricter=None):
    row = {'name': name, 'bytes': base64.b64encode(text.encode() if isinstance(text, str) else text).decode()}
    if stricter: row['stricter'] = stricter
    return row

HEADER = '<?xml version="1.0" encoding="UTF-8"?>\n<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">\n'
def plist(body): return HEADER + '<plist version="1.0">\n' + body + '\n</plist>\n'
raws = [
    raw('entities', plist('<string>&lt;a&gt; &amp; &quot;q&quot; &apos; &#65;&#x42;&#x1F680;</string>')),
    raw('cdata', plist('<string><![CDATA[<raw & text>]]></string>')),
    raw('commentsEverywhere', plist('<!-- a --><dict><!-- b --><key>k</key><!-- c --><integer>1</integer></dict>')),
    raw('integerWhitespace', plist('<integer> 42 </integer>')),
    raw('integerHex', plist('<integer>0x1F</integer>')),
    raw('integerPlus', plist('<integer>+5</integer>')),
    raw('integerNegativeZero', plist('<integer>-0</integer>')),
    raw('integerOverflow', plist('<integer>18446744073709551616</integer>')),
    raw('integerUnsignedMax', plist('<integer>18446744073709551615</integer>')),
    raw('integerMin', plist('<integer>-9223372036854775808</integer>')),
    raw('integerBelowMin', plist('<integer>-9223372036854775809</integer>')),
    raw('integerEmpty', plist('<integer></integer>')),
    raw('integerJunk', plist('<integer>12abc</integer>')),
    raw('realSpecial', plist('<array><real>nan</real><real>+infinity</real><real>-infinity</real><real>-0</real><real>1e3</real><real>.5</real></array>')),
    raw('realJunk', plist('<real>1.5x</real>')),
    raw('dateValid', plist('<date>2024-02-29T12:34:56Z</date>')),
    raw('dateInvalidDay', plist('<date>2023-02-29T12:34:56Z</date>')),
    raw('dateNoZone', plist('<date>2024-02-29T12:34:56</date>')),
    raw('dataWhitespace', plist('<data>\n\tAAEC\n\tAw==\n</data>')),
    raw('dataInvalid', plist('<data>A*==</data>')),
    raw('emptyElements', plist('<dict><key>a</key><string/><key>b</key><array/><key>c</key><dict/><key>d</key><data/></dict>')),
    raw('bareRoot', HEADER + '<dict><key>k</key><true/></dict>\n'),
    raw('noProlog', '<plist version="1.0"><string>x</string></plist>'),
    raw('duplicateKey', plist('<dict><key>a</key><integer>1</integer><key>a</key><integer>2</integer></dict>'), 'Duplicate keys are rejected instead of silently keeping one value'),
    raw('keyWithoutValue', plist('<dict><key>a</key></dict>')),
    raw('valueWithoutKey', plist('<dict><integer>1</integer></dict>')),
    raw('mismatchedClose', plist('<array><string>x</array></string>')),
    raw('unknownElement', plist('<float>1</float>')),
    raw('trailingGarbage', plist('<string>x</string>') + '<string>y</string>', 'Content after the root element is treated as corruption'),
    raw('truncated', plist('<dict><key>a</key><string>x</string>')[:-12]),
    raw('internalSubset', '<?xml version="1.0"?><!DOCTYPE plist [<!ENTITY x "boom">]><plist version="1.0"><string>&x;</string></plist>'),
    raw('utf16Declared', '<?xml version="1.0" encoding="UTF-16"?><plist version="1.0"><string>x</string></plist>'),
    raw('invalidUtf8', b'<?xml version="1.0" encoding="UTF-8"?><plist version="1.0"><string>\xff</string></plist>'),
    raw('realLeadingSpace', plist('<real> 1.5</real>')),
    raw('realTrailingSpace', plist('<real>1.5 </real>')),
    raw('realHexFloat', plist('<real>0x1p3</real>')),
    raw('realComma', plist('<real>1,5</real>')),
    raw('realLeadingZeros', plist('<real>007.25</real>')),
    raw('realExponentOnly', plist('<real>1e</real>')),
    raw('realSubnormal', plist('<real>4.9406564584124654e-324</real>')),
    raw('integerLeadingZeros', plist('<integer>007</integer>')),
    raw('integerNegativeHex', plist('<integer>-0x10</integer>')),
    raw('integerUpperHex', plist('<integer>0XfF</integer>')),
    raw('emptyString', plist('<string/>')),
    raw('emptyStringPair', plist('<string></string>')),
    raw('emptyData', plist('<data/>')),
    raw('emptyDataPair', plist('<data></data>')),
    raw('emptyArray', plist('<array/>')),
    raw('emptyDict', plist('<dict/>')),
    raw('emptyKey', plist('<dict><key/><true/></dict>')),
    raw('emptyKeyPair', plist('<dict><key></key><true/></dict>')),
    raw('trueNotSelfClosing', plist('<true></true>')),
    raw('dateMonth13', plist('<date>2024-13-01T00:00:00Z</date>')),
    raw('dateDay32', plist('<date>2024-01-32T00:00:00Z</date>')),
    raw('dateHour24', plist('<date>2024-01-01T24:00:00Z</date>')),
    raw('dateSecond60', plist('<date>2024-01-01T00:00:60Z</date>')),
    raw('dateWhitespace', plist('<date> 2024-01-01T00:00:00Z</date>')),
    raw('dateYear1', plist('<date>0001-01-01T00:00:00Z</date>')),
    raw('dataGarbageMiddle', plist('<data>AB=C</data>')),
    raw('dataUnpadded', plist('<data>AAECAw</data>')),
    raw('dataInvalidChars', plist('<data>AA!EC@Aw==</data>')),
    raw('stringNulReference', plist('<string>a&#0;b</string>')),
    raw('stringControlChar', plist('<string>a\x01b</string>')),
    raw('stringUnknownEntity', plist('<string>&nbsp;</string>')),
    raw('keyEntity', plist('<dict><key>a&amp;b</key><false/></dict>')),
    raw('plistVersionMissing', '<?xml version="1.0" encoding="UTF-8"?><plist><true/></plist>'),
    raw('uppercaseTag', plist('<TRUE/>')),
    raw('binaryTruncated', bytes.fromhex('62706c6973743030d101025161')),
    raw('binaryBadMagic', b'bplist01' + bytes(40)),
    raw('empty', b''),
]
# Hand-built bplist00 edge cases (offset sizes and references are explicit).
def bplist(objects, top=0, ref=1, off=1):
    out = b'bplist00'; offsets = []
    for obj in objects: offsets.append(len(out)); out += obj
    table = len(out)
    for o in offsets: out += o.to_bytes(off, 'big')
    out += bytes(6) + bytes([off, ref]) + len(objects).to_bytes(8, 'big') + top.to_bytes(8, 'big') + table.to_bytes(8, 'big')
    return out
raws += [
    raw('binaryCycle', bplist([b'\xa1\x00'])),
    raw('binarySharedLeaf', bplist([b'\xa3\x01\x01\x01', b'\x51x'])),
    raw('binaryNonStringKey', bplist([b'\xd1\x01\x01', b'\x10\x05'])),
    raw('binaryDuplicateKey', bplist([b'\xd2\x01\x01\x02\x02', b'\x51k', b'\x10\x01']), 'Duplicate keys are rejected instead of silently keeping one value'),
    raw('binaryUnpairedSurrogate', bplist([b'\x61\xd8\x00']), 'Unpaired UTF-16 surrogates have no UTF-8 form'),
    raw('binaryPairedSurrogate', bplist([b'\x62\xd8\x3d\xde\x80'])),
    raw('binaryRefOutOfRange', bplist([b'\xa1\x05'])),
    raw('binaryOffsetIntoTrailer', bplist([b'\x09'])[:-8] + (999).to_bytes(8, 'big')),
    raw('binaryInt128Unsigned', bplist([b'\x14' + bytes(8) + (2**64 - 1).to_bytes(8, 'big')])),
    raw('binaryInt128Negative', bplist([b'\x14' + bytes([255] * 8) + (2**64 - 5).to_bytes(8, 'big')])),
    raw('binaryInt128TooLarge', bplist([b'\x14' + (1).to_bytes(8, 'big') + bytes(8)])),
    raw('binaryFloat32', bplist([b'\x22' + struct.pack('>f', 0.75)])),
    raw('binaryExtendedCount', bplist([b'\x5f\x10\x0f' + b'abcdefghijklmno'])),
    raw('binaryUid', bplist([b'\x80\x07'])),
    raw('binaryNull', bplist([b'\x00'])),
    raw('binaryAsciiHighByte', bplist([b'\x51\xe9'])),
    raw('binaryAscii80', bplist([b'\x52\x80\x9f'])),
    raw('binaryAsciiNul', bplist([b'\x53a\x00b'])),
    raw('binaryUtf16Nul', bplist([b'\x61\x00\x00'])),
    raw('binaryDataExtended', bplist([b'\x4f\x10\x10' + bytes(range(16))])),
    raw('binaryCountWrongMarker', bplist([b'\x5f\x20\x01a'])),
    raw('binaryInt8Bytes', bplist([b'\x13' + (2**64 - 1).to_bytes(8, 'big')])),
    raw('binaryInt4Bytes', bplist([b'\x12\xff\xff\xff\xff'])),
    raw('binaryDict', bplist([b'\xd1\x01\x02', b'\x51k', b'\x08'])),
    raw('binaryDoubleZeroOffsetTable', bplist([b'\x09'])[:-8] + (8).to_bytes(8, 'big')),
    raw('binaryTopOutOfRange', bplist([b'\x09'], top=1)),
    raw('binaryDate', bplist([b'\x33' + struct.pack('>d', 1.5)])),
    raw('binaryReal16', bplist([b'\x24' + bytes(16)])),
    raw('binaryTwoByteRefs', bplist([b'\xa1\x00\x01', b'\x09'], ref=2)),
]

S = 'summonShortcut'
settings = [
    ('empty', {}),
    ('fullValid', dict(hudSettingsSchemaVersion=i(1), displayMode=s('always'), displayDuration=real(7.5), accentHex=s('12ABEF'),
        theme=s('light'), scale=real(1.2), placement=s('custom'), customScreenID=i(69734272), customPositionX=real(0.25),
        customPositionY=real(0.75), language=s('japanese'), hudScale=real(1.5), hudOffsetX=real(-0.125), hudOffsetY=real(0.25),
        parallaxIntensity=real(0.5), perspectiveIntensity=real(1.75), backgroundDarkness=real(0.4), blurAmount=real(0.2),
        reduceMotion=b(True), ambientAnimation=b(False), closeOnFocusLost=b(False), openOnActiveDisplay=b(False),
        hudDisplayUUID=s('3f2504e0-4f89-41d3-9a0c-0305e82c3301'), hudDisplayName=s('  Studio Display \n'), launchAtLogin=b(False),
        batteryAlertsEnabled=b(False), devicePopupEnabled=b(False), lowPowerVisualMode=b(True), applicationIcon=s('gameStrength'),
        clockFormat=s('twelveHour'), clockStyle=s('stacked'), centerLogo=s('custom'), centerLogoRevision=s('9b2d1c3a-1111-4222-8333-444455556666'),
        alertMetric=s('disk'), summonShortcut=shortcut(keyCode=4, modifiers=3), hasLaunched=b(True), **{'orbipom.bestScore.v1': i(120)})),
    ('legacyAccentMigrates', dict(accentHex=s('d9f36b'))),
    ('legacyAccentKeptWithMarker', dict(hudSettingsSchemaVersion=i(1), accentHex=s('D9F36B'))),
    ('legacyAccentPaddedNotMigrated', dict(accentHex=s(' D9F36B'))),
    ('accentNormalized', dict(hudSettingsSchemaVersion=i(1), accentHex=s('  #fad41f\n'))),
    ('accentLigature', dict(hudSettingsSchemaVersion=i(1), accentHex=s('ﬀ0000'))),
    ('accentInvalid', dict(hudSettingsSchemaVersion=i(1), accentHex=s('12345'))),
    ('accentNumber', dict(hudSettingsSchemaVersion=i(1), accentHex=i(123456))),
    ('accentHashes', dict(hudSettingsSchemaVersion=i(1), accentHex=s('##A1B2C3'))),
    ('numbersOutOfRange', dict(displayDuration=real(0), scale=real(3), hudScale=real(0.1), hudOffsetX=real(-1), hudOffsetY=real(1),
        customPositionX=real(-0.0), customPositionY=real(2), backgroundDarkness=real(1.5), blurAmount=real(-1), parallaxIntensity=real(9),
        perspectiveIntensity=real(-0.0))),
    ('numbersNonFinite', dict(displayDuration=real(nan), scale=real(inf), hudScale=real(-inf), hudOffsetX=real(nan), customPositionX=real(nan),
        customPositionY=real(inf), backgroundDarkness=real(nan), blurAmount=real(inf), parallaxIntensity=real(nan), perspectiveIntensity=real(-inf))),
    ('numbersAsStrings', dict(displayDuration=s('12.5'), scale=s(' 1.5x'), hudScale=s('abc'), hudOffsetX=s('-0.25'), hudOffsetY=s('1e-1'),
        customPositionX=s('.5'), customPositionY=s('-.5'), backgroundDarkness=s('inf'), blurAmount=s('  0.3  '), parallaxIntensity=s('+1.5'),
        perspectiveIntensity=s('1.'))),
    ('numbersAsOtherTypes', dict(displayDuration=b(True), scale=data(b'1'), hudScale=date(5.0), hudOffsetX=arr(real(0.1)),
        customPositionX=i(1), customPositionY=b(False), backgroundDarkness=dd({}), blurAmount=i(-3))),
    ('boolsAsStrings', dict(reduceMotion=s('YES'), ambientAnimation=s('no'), closeOnFocusLost=s('true'), openOnActiveDisplay=s('0'),
        launchAtLogin=s(' 0001'), batteryAlertsEnabled=s('-1'), devicePopupEnabled=s('t'), lowPowerVisualMode=s('0.5'))),
    ('boolsAsNumbers', dict(reduceMotion=i(2), ambientAnimation=i(0), closeOnFocusLost=real(0.5), openOnActiveDisplay=real(0.0),
        launchAtLogin=data(b'\x01'), batteryAlertsEnabled=date(1.0), devicePopupEnabled=arr(), lowPowerVisualMode=real(-0.25))),
    ('enumsInvalid', dict(displayMode=s('ALWAYS'), theme=s('Dark'), placement=i(1), language=s('french'), clockFormat=s(''),
        clockStyle=i(3), centerLogo=s('Custom'), alertMetric=s('gpu'), applicationIcon=s('nope'))),
    ('enumsRetained', dict(applicationIcon=s('originium'), clockStyle=s('rail'), alertMetric=s('network'), language=s('traditionalChinese'))),
    ('screenIDs1', dict(customScreenID=i(1))), ('screenIDs2', dict(customScreenID=i(4294967296))), ('screenIDs3', dict(customScreenID=i(-1))),
    ('screenIDs4', dict(customScreenID=real(1.5))), ('screenIDs5', dict(customScreenID=b(True))), ('screenIDs6', dict(customScreenID=s('5'))),
    ('screenIDs7', dict(customScreenID=i(2**64 - 1))), ('screenIDs8', dict(customScreenID=real(4294967295.0))),
    ('displayIdentity', dict(hudDisplayUUID=s('3F2504E0-4F89-41D3-9A0C-0305E82C3301'), hudDisplayName=s('  ' + 'N' * 140 + '  '))),
    ('displayIdentityInvalidUUID', dict(hudDisplayUUID=s('{3F2504E0-4F89-41D3-9A0C-0305E82C3301}'), hudDisplayName=s('Desk'))),
    ('displayIdentityBlankName', dict(hudDisplayUUID=s('3f2504e0-4f89-41d3-9a0c-0305e82c3301'), hudDisplayName=s(' \n\t '))),
    ('displayIdentityNumberName', dict(hudDisplayUUID=s('3f2504e0-4f89-41d3-9a0c-0305e82c3301'), hudDisplayName=i(42))),
    ('centerLogoInvalidRevision', dict(centerLogo=s('custom'), centerLogoRevision=s('not-a-uuid'))),
    ('shortcutDefaultExplicit', {S: shortcut(keyCode=50, modifiers=1)}),
    ('shortcutInvalidJSON', {S: data(b'{"keyCode":')}),
    ('shortcutUnnamedKey', {S: shortcut(keyCode=200, modifiers=1)}),
    ('shortcutNoModifiers', {S: shortcut(keyCode=4, modifiers=0)}),
    ('shortcutThreeModifiers', {S: shortcut(keyCode=4, modifiers=7)}),
    ('shortcutUnsupportedModifier', {S: shortcut(keyCode=4, modifiers=16)}),
    ('shortcutReservedCommand', {S: shortcut(keyCode=8, modifiers=8)}),
    ('shortcutReservedCommandShift', {S: shortcut(keyCode=6, modifiers=12)}),
    ('shortcutCommandShiftAllowed', {S: shortcut(keyCode=4, modifiers=12)}),
    ('shortcutFloatKey', {S: data(b'{"keyCode":50.0,"modifiers":1}')}),
    ('shortcutFractionalKey', {S: data(b'{"keyCode":50.5,"modifiers":1}')}),
    ('shortcutMissingModifiers', {S: data(b'{"keyCode":4}')}),
    ('shortcutExtraField', {S: data(b'{"keyCode":122,"modifiers":2,"note":"F1"}')}),
    ('shortcutAsString', {S: s('{"keyCode":4,"modifiers":3}')}),
    ('shortcutNegativeModifiers', {S: shortcut(keyCode=4, modifiers=-1)}),
    ('shortcutLargeKey', {S: shortcut(keyCode=70000, modifiers=1)}),
    ('bestScoreNegative', {'orbipom.bestScore.v1': i(-5)}), ('bestScoreReal', {'orbipom.bestScore.v1': real(12.9)}),
    ('bestScoreString', {'orbipom.bestScore.v1': s('77abc')}), ('bestScoreBool', {'orbipom.bestScore.v1': b(True)}),
    ('bestScoreUnsignedMax', {'orbipom.bestScore.v1': i(2**64 - 1)}), ('bestScoreData', {'orbipom.bestScore.v1': data(b'9')}),
    ('hasLaunchedString', dict(hasLaunched=s('YES'))),
    ('updaterAndUnknown', dict(HUDUpdateAutomaticallyInstall=b(False), HUDUpdateLastNotifiedRelease=s('1.2.0'), SUEnableAutomaticChecks=b(True),
        SULastCheckTime=date(700000000.0), **{'NSWindow Frame Settings': s('0 0 100 100'), 'futureSetting': d(nested=arr(i(1)))})),
    ('probes', dict(p1=s('YES'), p2=s('no'), p3=s('Tru'), p4=s('  +0009'), p5=s('-0'), p6=s('0x10'), p7=s('1e400'), p8=s('-1e400'),
        p9=s('1e-400'), p10=s('9223372036854775808'), p11=s('-9223372036854775809'), p12=s(' 　 42'), p13=s('4.9e1'),
        p14=real(1e30), p15=real(-1e30), p16=real(-0.0), p17=i(2**63), p18=real32(0.1), p19=s('nan'), p20=s(''), p21=s('.'),
        p22=s('+.5e1x'), p23=s(' 7'), p24=b(False), p25=date(3.0), p26=data(b'12'), p27=i(-7), p28=real(2.5), p29=real(-2.5),
        p30=s('١٢'), p31=real(0.1 + 0.2), p32=real32(1 / 3), p33=s('1'), p34=s('yes'), p35=s('TRUE'), p36=s(' YES'), p37=s('YES '),
        p38=s('NO'), p39=s('12 '), p40=s('٣'), p41=s('１２'), p42=s('1,5'), p43=real(1234567.0), p44=real(12345678901234567890.0),
        p45=real(0.000001), p46=real(1e-7), p47=real(123456789012345.6), p48=real32(16777217.0), p49=s('\u0663.5'), p50=s('Yes\u0301'),
        p51=s('2'), p52=s('01'), p53=s('Y'), p54=s('yEs'), p55=s('1 '), p56=s('+1'), p57=s('0'), p58=s('\uff11'))),
]

doc = {'plist': {'values': [{'name': n, 'value': v} for n, v in values], 'raw': raws},
       'settings': [{'name': n, 'values': v} for n, v in settings]}
(build / 'input.json').write_text(json.dumps(doc))
sources = sorted(p for p in (root / 'Sources').glob('*.swift') if p.name != 'main.swift')
# The pinned Swift 6.1 toolchain needs the macOS 15.5 SDK (as map_store_reference.py).
pinned = Path('/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk')
sdk = str(pinned) if pinned.exists() else subprocess.run(['bash', str(root / 'scripts/build.sh'), '--print-sdk'], capture_output=True, text=True, check=True).stdout.strip().splitlines()[-1]
cache = build / 'module-cache'
cache.mkdir(parents=True, exist_ok=True)
exe = build / 'mac-import-reference'
frameworks = []
for f in ['Cocoa', 'IOKit', 'CoreAudio', 'Quartz', 'Carbon', 'ServiceManagement', 'Metal', 'MetalKit', 'WebKit', 'Security', 'PDFKit', 'JavaScriptCore']:
    frameworks += ['-framework', f]
oracle = root / 'windows/tools/mac_import_reference.swift'
if not exe.exists() or exe.stat().st_mtime < max(p.stat().st_mtime for p in sources + [oracle]):
    log = build / 'compile.log'
    with open(log, 'w') as handle:
        result = subprocess.run(['xcrun', 'swiftc', '-swift-version', '5', '-Onone', '-whole-module-optimization', '-parse-as-library',
            '-module-name', 'EndfieldMacImportReference', '-D', 'HUD_WATCH_MOTION_PREVIEW', '-sdk', sdk, '-module-cache-path', str(cache)]
            + frameworks + ['-lsqlite3', '-lz'] + [str(p) for p in sources] + [str(oracle), '-o', str(exe)], stdout=handle, stderr=subprocess.STDOUT,
            env=dict(os.environ, SDKROOT=sdk))
    if result.returncode:
        sys.exit('Swift oracle failed to compile; see ' + str(log))
digests = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
with tempfile.TemporaryDirectory(prefix='ehud-mac-import-oracle-') as home:
    subprocess.run([str(exe), str(build / 'input.json'), str(build / 'output.json')], cwd=str(build),
                   env=dict(os.environ, CFFIXED_USER_HOME=home), check=True)
for name, digest in digests.items():
    assert hashlib.sha256((root / name).read_bytes()).hexdigest() == digest, 'Original source changed during the oracle run'
out = json.loads((build / 'output.json').read_text())

# Golden export: Mac stores -> exporter -> fixture (every file base64).
with tempfile.TemporaryDirectory(prefix='ehud-mac-import-golden-') as temporary:
    work = Path(temporary)
    with tempfile.TemporaryDirectory(prefix='ehud-mac-import-golden-home-') as home:
        subprocess.run([str(exe), '--golden', str(work)], cwd=str(build), env=dict(os.environ, CFFIXED_USER_HOME=home), check=True)
    exported = work / 'export'
    subprocess.run([sys.executable, '-I', str(root / 'windows/tools/mac_import_exporter.py'), str(exported), '--source-root', str(work / 'EndfieldCharge'),
                    '--defaults-plist', str(work / 'domain.plist'), '--app-info', str(root / 'Resources/Info.plist')], check=True)
    golden_expected = json.loads((work / 'expected.json').read_text())
    files = {}
    for path in sorted(exported.rglob('*')):
        if path.is_file():
            files[path.relative_to(exported).as_posix()] = base64.b64encode(path.read_bytes()).decode()
manifest = json.loads(base64.b64decode(files['manifest.json']))
listed = [entry['path'] for entry in manifest['files']]
# The account cache is rewritten (requiresReconnect) and Mac shelf records are
# not Windows shelf records; every other store must arrive byte-identical.
identical = [p for p in listed if p.startswith('EndfieldCharge/') and not p.startswith(('EndfieldCharge/Account/', 'EndfieldCharge/FileShelf/'))]
stores = {'settings': 'importedWithWarnings', 'notes': 'importedWithWarnings', 'archive': 'importedWithWarnings', 'profile': 'imported',
          'fileShelf': 'importedWithWarnings', 'reader': 'importedWithWarnings', 'calendar': 'importedWithWarnings', 'worldMap': 'imported',
          'appShortcuts': 'importedWithWarnings', 'eventLog': 'imported', 'account': 'importedWithWarnings', 'centerLogo': 'imported'}
golden = {'provenance': {'generator': 'windows/tools/mac_import_reference.py + windows/tools/mac_import_exporter.py',
                         'note': 'Synthetic records written by the unchanged macOS stores; bookmarks and file identities are synthetic'},
          'files': files,
          'expected': {'stores': stores, 'relinkItems': 5, 'profileSyncLocked': golden_expected['profileSyncLocked'], 'hasLaunched': golden_expected['settings'].pop('hasLaunched'),
                       'byteIdentical': identical, 'settings': golden_expected['settings'], 'foundationDate': golden_expected['foundationDate']}}
provenance = {'generator': 'windows/tools/mac_import_reference.py', 'sources': {k: digests[k] for k in
    ['Sources/Models.swift', 'Sources/SummonShortcut.swift', 'Sources/OrbiPomSession.swift', 'Sources/HUDApplicationIcon.swift']}}
fixtures = root / 'windows/tests/fixtures'
(fixtures / 'plist_mac_import_source.json').write_text(json.dumps({'provenance': provenance, **out['plist']}, ensure_ascii=False, indent=1, sort_keys=True) + '\n')
(fixtures / 'mac_import_settings_source.json').write_text(json.dumps({'provenance': provenance, 'cases': out['settings']}, ensure_ascii=False, indent=1, sort_keys=True) + '\n')
(fixtures / 'mac_import_golden_export.json').write_text(json.dumps(golden, ensure_ascii=False, indent=1, sort_keys=True) + '\n')
print(f"PASS {len(files)} golden export files; {len(out['plist']['encoded'])} encoded + {len(out['plist']['raw'])} raw plist cases, {len(out['settings'])} settings cases")
