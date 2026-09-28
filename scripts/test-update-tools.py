#!/usr/bin/env python3
"""Temporary release-metadata fixtures; never signs/publishes a real release."""
import copy
import importlib.util
import pathlib
import plistlib
import tempfile
import zipfile
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("appcast", ROOT / "scripts/validate-appcast.py")
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
info = m.load_info(ROOT / "Resources/Info.plist")
version = info["CFBundleShortVersionString"]
archive_name = f"EndfieldHUD-{version}-macOS.zip"
channel = "<sparkle:channel>preview</sparkle:channel>" if "-preview." in info["HUDReleaseTag"] else ""
# Metadata parsing fixtures only. This syntactically valid signature must never
# pass the separate cryptographic verifier, which is tested in Swift.
signature = "A" * 86 + "=="
xml = f'''<rss version="2.0" xmlns:sparkle="{m.NS[1:-1]}"><channel><item>
<sparkle:version>{info["CFBundleVersion"]}</sparkle:version>
<sparkle:shortVersionString>{info["HUDReleaseTag"][1:]}</sparkle:shortVersionString>
<sparkle:minimumSystemVersion>10.15.4</sparkle:minimumSystemVersion>{channel}
<enclosure url="{m.DOWNLOAD}{info["HUDReleaseTag"]}/{archive_name}" sparkle:edSignature="{signature}" length="1" type="application/octet-stream"/>
</item></channel></rss>'''
checks = 0

def rejects(action):
    global checks
    try:
        action()
    except (ValueError, KeyError):
        checks += 1
        return
    raise AssertionError("Malformed fixture accepted")


with tempfile.TemporaryDirectory(prefix="endfield-update-fixtures-") as temporary:
    temporary = pathlib.Path(temporary)
    feed = temporary / "appcast.xml"
    feed.write_text(xml)
    m.check_feed(feed, info)
    checks += 1
    changes = [
        (m.DOWNLOAD, "https://example.org/"),
        ("/releases/download/", "/releases/latest/"),
        (archive_name, "unrelated.zip"),
        (f'length="1"', 'length="0"'),
        (signature, "invalid"),
        (f'<sparkle:shortVersionString>{info["HUDReleaseTag"][1:]}</sparkle:shortVersionString>', "<sparkle:shortVersionString>9.9.9</sparkle:shortVersionString>"),
        ("</item>", "<sparkle:channel>incorrect</sparkle:channel></item>") if not channel else (channel, ""),
        ("10.15.4", "10.15.0"),
        ("</item>", "<sparkle:releaseNotesLink>https://example.org/</sparkle:releaseNotesLink></item>"),
        ("</item>", "<sparkle:deltas/></item>"),
        ("<channel>", "<channel><item/>"),
        (f'/{archive_name}"', f'/{archive_name}?replacement=1"'),
        ("<rss ", '<!DOCTYPE rss [<!ENTITY test "bad">]><rss '),
    ]
    for before, after in changes:
        feed.write_text(xml.replace(before, after))
        rejects(lambda: m.check_feed(feed, info))
    feed.write_text('<rss version="2.0"><channel/></rss>')
    m.check_feed(feed, info, allow_empty=True)
    rejects(lambda: m.check_feed(feed, info))
    checks += 1
    archive = temporary / archive_name
    def write_archive(extra=None, plist=info):
        with zipfile.ZipFile(archive, "w") as output:
            output.writestr("EndfieldHUD.app/Contents/Info.plist", plistlib.dumps(plist))
            if extra:
                output.writestr(*extra)
    write_archive()
    m.check_archive(archive, info)
    checks += 1
    for path in ["../escape", "/absolute", "Other.app/Contents/Info.plist", "EndfieldHUD.app/../../escape"]:
        write_archive((path, "bad"))
        rejects(lambda: m.check_archive(archive, info))
    link = zipfile.ZipInfo("EndfieldHUD.app/Contents/escape")
    link.create_system = 3
    link.external_attr = 0o120777 << 16
    write_archive((link, "../../../outside"))
    rejects(lambda: m.check_archive(archive, info))
    wrong = copy.deepcopy(info)
    wrong["CFBundleIdentifier"] = "other.app"
    write_archive(plist=wrong)
    rejects(lambda: m.check_archive(archive, info))
print(f"PASS: {checks} release metadata, URL, version, ZIP and symlink checks")
