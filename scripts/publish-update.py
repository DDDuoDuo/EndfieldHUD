#!/usr/bin/env python3
"""Publish only an already verified archive/feed to an existing GitHub release."""
import base64
import hashlib
import json
import pathlib
import plistlib
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parent.parent
REPO = "DDDuoDuo/EndfieldHUD"
NS = "{http://www.andymatuschak.org/xml-namespaces/sparkle}"


def gh(*args):
    return subprocess.run(["gh", *args], check=True, capture_output=True, text=True).stdout


def item_map(path):
    return {item.findtext(NS + "version"): ET.canonicalize(ET.tostring(item, encoding="unicode"), strip_text=True) for item in ET.parse(path).findall("channel/item")}


def main():
    if len(sys.argv) != 2:
        raise ValueError("Usage: publish-update.py RELEASE_ZIP")
    archive = pathlib.Path(sys.argv[1]).resolve()
    info_path = ROOT / "Resources/Info.plist"
    feed = ROOT / "updates/appcast.xml"
    validator = str(ROOT / "scripts/validate-appcast.py")
    verifier = str(ROOT / "scripts/verify-update-signature.sh")
    subprocess.run([sys.executable, validator, "archive", str(archive), str(info_path)], check=True)
    signature = subprocess.run([sys.executable, validator, "feed", str(feed), str(info_path), "--archive", str(archive), "--print-signature"], check=True, capture_output=True, text=True).stdout.strip()
    subprocess.run([verifier, str(info_path), "--archive", str(archive), signature], check=True)
    subprocess.run([verifier, str(info_path), "--feed", str(feed)], check=True)
    info = plistlib.loads((ROOT / "Resources/Info.plist").read_bytes())
    tag = info["HUDReleaseTag"]
    feed = ROOT / "updates/appcast.xml"
    release = json.loads(gh("release", "view", tag, "--repo", REPO, "--json", "tagName,isDraft,assets"))
    if release["isDraft"] or release["tagName"] != tag:
        raise ValueError("Create/publish the intended GitHub release first; this script does not create releases.")
    endpoint = f"repos/{REPO}/contents/updates/appcast.xml"
    remote = None
    try:
        remote = json.loads(gh("api", endpoint + "?ref=main"))
    except subprocess.CalledProcessError as error:
        if "HTTP 404" not in error.stderr:
            raise
    with tempfile.TemporaryDirectory(prefix="endfield-publish-") as temporary:
        temporary = pathlib.Path(temporary)
        if remote:
            old = temporary / "appcast.xml"
            old.write_bytes(base64.b64decode(remote["content"]))
            subprocess.run([str(ROOT / "scripts/verify-update-signature.sh"), str(ROOT / "Resources/Info.plist"), "--feed", str(old)], check=True)
            local_items = item_map(feed)
            for build, content in item_map(old).items():
                if local_items.get(build) != content:
                    raise ValueError("Local feed would remove/change a published entry. Fetch the latest main branch and regenerate.")
        if not any(asset["name"] == archive.name for asset in release["assets"]):
            gh("release", "upload", tag, str(archive), "--repo", REPO)
        # Never replace an existing asset. Check the public URL before advertising
        # it; this also prevents accidentally publishing a feed for a draft asset.
        downloaded = temporary / archive.name
        url = f"https://github.com/{REPO}/releases/download/{tag}/{archive.name}"
        subprocess.run(["curl", "--fail", "--location", "--silent", "--show-error", "--retry", "3", "--proto", "=https", "--proto-redir", "=https", url, "-o", str(downloaded)], check=True)
        if hashlib.sha256(downloaded.read_bytes()).digest() != hashlib.sha256(archive.read_bytes()).digest():
            raise ValueError("Published release asset differs from the signed local archive; no feed was published.")
        body = {"message": f"Publish signed update feed for {tag}", "branch": "main", "content": base64.b64encode(feed.read_bytes()).decode()}
        if remote:
            body["sha"] = remote["sha"]
        payload = temporary / "payload.json"
        payload.write_text(json.dumps(body))
        gh("api", endpoint, "--method", "PUT", "--input", str(payload))
        print("Published the signed update feed after verifying the release asset. Pull main before further feed edits.")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
