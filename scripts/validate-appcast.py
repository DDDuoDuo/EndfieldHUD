#!/usr/bin/env python3
"""Release metadata checks. Cryptographic verification is separate and mandatory."""
import argparse
import base64
import pathlib
import plistlib
import posixpath
import re
import stat
import sys
import urllib.parse
import xml.etree.ElementTree as ET
import zipfile

NS = "{http://www.andymatuschak.org/xml-namespaces/sparkle}"
REPOSITORY = "DDDuoDuo/EndfieldHUD"
DOWNLOAD = f"https://github.com/{REPOSITORY}/releases/download/"
FEED = f"https://raw.githubusercontent.com/{REPOSITORY}/main/updates/appcast.xml"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def load_info(path):
    with open(path, "rb") as stream:
        info = plistlib.load(stream)
    require(info.get("CFBundleIdentifier") == "io.github.endfieldcharge.EndfieldCharge", "Unexpected bundle identity")
    version = info.get("CFBundleShortVersionString", "")
    require(re.fullmatch(r"\d+\.\d+\.\d+", version), "Invalid app version")
    require(re.fullmatch(r"[1-9]\d*", info.get("CFBundleVersion", "")), "Build number must be a positive integer")
    tag = info.get("HUDReleaseTag", "")
    require(re.fullmatch(r"v" + re.escape(version) + r"(?:-preview\.[1-9]\d*)?", tag), "Release tag does not match version")
    require(info.get("SUFeedURL") == FEED, "Unexpected update feed")
    require(info.get("SUVerifyUpdateBeforeExtraction") is True and info.get("SURequireSignedFeed") is True,
            "Release must require archive and feed signatures")
    require(len(base64.b64decode(info.get("SUPublicEDKey", ""), validate=True)) == 32, "Invalid update public key")
    return info


def check_archive(path, info):
    require(path.name == f'EndfieldHUD-{info["CFBundleShortVersionString"]}-macOS.zip', "Unexpected release archive filename")
    with zipfile.ZipFile(path) as archive:
        require(archive.testzip() is None, "Corrupt release ZIP")
        seen = set()
        for entry in archive.infolist():
            name = entry.filename
            require(name not in seen, "Duplicate ZIP path")
            seen.add(name)
            require(not name.startswith("/") and "\\" not in name and ".." not in name.split("/"), "Unsafe ZIP path")
            require(name.startswith(("EndfieldHUD.app/", "__MACOSX/")), "ZIP must contain only EndfieldHUD.app")
            if stat.S_ISLNK(entry.external_attr >> 16):
                target = archive.read(entry).decode("utf-8")
                resolved = posixpath.normpath(posixpath.join(posixpath.dirname(name), target))
                require(not target.startswith("/") and resolved.startswith("EndfieldHUD.app/"), "Escaping ZIP symlink")
        bundled = plistlib.loads(archive.read("EndfieldHUD.app/Contents/Info.plist"))
        require(bundled == info, "Archive Info.plist differs from current release metadata")


def check_feed(path, info, archive=None, allow_empty=False):
    data = path.read_bytes()
    require(len(data) <= 4 * 1024 * 1024, "Appcast is too large")
    require(b"<!DOCTYPE" not in data and b"<!ENTITY" not in data, "Appcast must not contain entities or DTDs")
    root = ET.fromstring(data)
    require(root.tag == "rss" and root.get("version") == "2.0", "Expected RSS 2.0")
    channels = root.findall("channel")
    require(len(channels) == 1, "Expected one update channel")
    items = channels[0].findall("item")
    require(len(items) <= 64 and (items or allow_empty), "Feed is empty or has too many entries")
    builds = set()
    selected = None
    for item in items:
        build = item.findtext(NS + "version", "")
        require(re.fullmatch(r"[1-9]\d*", build) and build not in builds, "Invalid or duplicate feed build")
        builds.add(build)
        enclosures = item.findall("enclosure")
        require(len(enclosures) == 1, "Expected one full update archive per item")
        enclosure = enclosures[0]
        url = enclosure.get("url", "")
        parsed = urllib.parse.urlsplit(url)
        require(url.startswith(DOWNLOAD) and not parsed.query and not parsed.fragment, "Update URL must be an immutable release asset in this repository")
        relative = url[len(DOWNLOAD):].split("/")
        require(len(relative) == 2, "Invalid release URL")
        version = item.findtext(NS + "shortVersionString", "")
        require(re.fullmatch(r"\d+\.\d+\.\d+(?:-preview\.[1-9]\d*)?", version), "Invalid displayed version")
        require(relative[0] == "v" + version, "Displayed version must include the full release tag suffix")
        base_version = version.split("-", 1)[0]
        expected_channel = "preview" if "-preview." in relative[0] else ""
        require(item.findtext(NS + "channel", "") == expected_channel, "Update channel does not match release tag")
        require(relative[1] == f"EndfieldHUD-{base_version}-macOS.zip", "Unexpected update asset filename")
        signature = enclosure.get(NS + "edSignature", "")
        require(len(base64.b64decode(signature, validate=True)) == 64, "Missing or invalid archive signature")
        require(enclosure.get("type") == "application/octet-stream", "Unexpected enclosure type")
        require(int(enclosure.get("length", "0")) > 0, "Invalid archive length")
        minimum = item.findtext(NS + "minimumSystemVersion", "")
        require(re.fullmatch(r"\d+\.\d+(?:\.\d+)?", minimum), "Missing minimum OS")
        require(tuple(int(x) for x in minimum.split(".")) >= (10, 15, 4), "Minimum OS below supported target")
        require(item.find(NS + "releaseNotesLink") is None, "Embed release notes so no separate unsigned notes are fetched")
        require(item.find(NS + "deltas") is None, "This release workflow publishes full archives only")
        if build == info["CFBundleVersion"]:
            require(url == DOWNLOAD + info["HUDReleaseTag"] + "/" + f'EndfieldHUD-{info["CFBundleShortVersionString"]}-macOS.zip', "Current release URL mismatch")
            if archive:
                require(int(enclosure.get("length")) == archive.stat().st_size, "Archive byte length mismatch")
            selected = signature
    if archive:
        require(selected is not None, "Current release missing from feed")
    return selected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="mode", required=True)
    arc = sub.add_parser("archive")
    arc.add_argument("archive", type=pathlib.Path)
    arc.add_argument("info", type=pathlib.Path)
    feed = sub.add_parser("feed")
    feed.add_argument("feed", type=pathlib.Path)
    feed.add_argument("info", type=pathlib.Path)
    feed.add_argument("--archive", type=pathlib.Path)
    feed.add_argument("--allow-empty", action="store_true")
    feed.add_argument("--print-signature", action="store_true")
    args = parser.parse_args()
    info = load_info(args.info)
    if args.mode == "archive":
        check_archive(args.archive, info)
        print("Validated release archive metadata and paths")
    else:
        signature = check_feed(args.feed, info, args.archive, args.allow_empty)
        print(signature if args.print_signature else "Validated appcast metadata")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, ET.ParseError, zipfile.BadZipFile, KeyError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
