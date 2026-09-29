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
    def write_archive(extra=None, plist=info, path=archive):
        with zipfile.ZipFile(path, "w") as output:
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

    # A same-version hotfix gets a new build and asset URL while the original
    # published item and filename remain valid.
    hotfix_info = copy.deepcopy(info)
    hotfix_info["CFBundleVersion"] = "10"
    hotfix_name = f"EndfieldHUD-{version}-build10-macOS.zip"
    hotfix_archive = temporary / hotfix_name
    write_archive(plist=hotfix_info, path=hotfix_archive)
    m.check_archive(hotfix_archive, hotfix_info)
    checks += 1
    old_xml = xml.replace(f'<sparkle:version>{info["CFBundleVersion"]}</sparkle:version>',
                          '<sparkle:version>9</sparkle:version>')
    hotfix_xml = xml.replace(f'<sparkle:version>{info["CFBundleVersion"]}</sparkle:version>',
                            '<sparkle:version>10</sparkle:version>').replace(archive_name, hotfix_name)
    hotfix_xml = hotfix_xml.replace('length="1"', f'length="{hotfix_archive.stat().st_size}"')
    combined = ET.fromstring(hotfix_xml)
    combined.find("channel").append(ET.fromstring(old_xml).find("channel/item"))
    feed.write_text(ET.tostring(combined, encoding="unicode"))
    m.check_feed(feed, hotfix_info, hotfix_archive)
    checks += 1
    rejects(lambda: m.check_feed(feed, hotfix_info, archive))
    # Both names can be valid separately, but one URL cannot identify two builds.
    feed.write_text(ET.tostring(combined, encoding="unicode").replace(hotfix_name, archive_name))
    rejects(lambda: m.check_feed(feed, hotfix_info))
    for invalid_build in ["9", "11", "010", "0"]:
        wrong_name = f"EndfieldHUD-{version}-build{invalid_build}-macOS.zip"
        rejects(lambda: m.check_archive(temporary / wrong_name, hotfix_info))
        feed.write_text(hotfix_xml.replace(hotfix_name, wrong_name))
        rejects(lambda: m.check_feed(feed, hotfix_info))
    feed.write_text(hotfix_xml.replace(f'length="{hotfix_archive.stat().st_size}"', 'length="1"'))
    rejects(lambda: m.check_feed(feed, hotfix_info, hotfix_archive))
print(f"PASS: {checks} release metadata, URL, version, ZIP and symlink checks")
