#!/usr/bin/env python3
"""Build Windows-only MSIX/App Installer assets; signatures remain mandatory."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from urllib.parse import urlsplit
import xml.etree.ElementTree as ET
import zipfile

from stage_resources import checked_file, json_bytes, verify_resources, windows_io_path

CONSUMER_IDENTITY = "DDDuoDuo.EndfieldHUD.Windows"
PREVIEW_IDENTITY = "DDDuoDuo.EndfieldHUD.Windows.Preview"
FOUNDATION = "http://schemas.microsoft.com/appx/manifest/foundation/windows10"
UAP = "http://schemas.microsoft.com/appx/manifest/uap/windows10"
RESTRICTED = "http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities"
APP_INSTALLER = "http://schemas.microsoft.com/appx/appinstaller/2021"


def digest(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1048576), b""):
            result.update(chunk)
    return result.hexdigest()


def run_sdk(command: list[str]) -> None:
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
    if result.returncode != 0:
        raise ValueError("Windows SDK package/signature validation failed:\n" + "\n".join(result.stdout.splitlines()[-16:]))


def package_version(value: str) -> str:
    if not re.fullmatch(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)", value):
        raise ValueError("MSIX package version must contain four integer components")
    parts = [int(part) for part in value.split(".")]
    if any(part > 65535 for part in parts):
        raise ValueError("MSIX version components must be 0..65535")
    return value


def https_uri(value: str, *, windows_feed: bool = False) -> str:
    parsed = urlsplit(value)
    if parsed.scheme != "https" or not parsed.hostname or parsed.username or parsed.password or parsed.fragment or parsed.query:
        raise ValueError("Release URLs must be direct HTTPS URLs without credentials, query or fragment")
    if windows_feed and "windows" not in parsed.path.lower():
        raise ValueError("Windows update feed must have a dedicated Windows path/asset name")
    if "/releases/latest/" in parsed.path:
        raise ValueError("A shared GitHub latest-release URL could select Mac assets; use a Windows-only feed")
    return value


def sdk_tool(name: str, explicit: Path | None = None) -> Path:
    if explicit:
        if not explicit.is_file():
            raise ValueError("Windows SDK tool is missing: " + str(explicit))
        return explicit.resolve()
    root = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Windows Kits" / "10" / "bin"
    matches = [path for path in root.glob("*/x64/" + name) if re.fullmatch(r"[0-9]+(?:\.[0-9]+){3}", path.parents[1].name)]
    matches.sort(key=lambda path: tuple(int(part) for part in path.parents[1].name.split(".")), reverse=True)
    if not matches:
        raise ValueError("Windows SDK x64 " + name + " is required")
    return matches[0]


def appx_manifest(*, identity: str, publisher: str, version: str, min_os: str, candidate: bool) -> bytes:
    package_version(version)
    package_version(min_os)
    if not publisher.strip() or "\x00" in publisher:
        raise ValueError("A certificate publisher distinguished name is required")
    ET.register_namespace("", FOUNDATION)
    ET.register_namespace("uap", UAP)
    ET.register_namespace("rescap", RESTRICTED)
    root = ET.Element("{" + FOUNDATION + "}Package", {"IgnorableNamespaces": "uap rescap"})
    ET.SubElement(root, "{" + FOUNDATION + "}Identity", {"Name": identity, "Publisher": publisher, "Version": version, "ProcessorArchitecture": "x64"})
    properties = ET.SubElement(root, "{" + FOUNDATION + "}Properties")
    for name, value in (("DisplayName", "EndfieldHUD Windows preview" if candidate else "EndfieldHUD"), ("PublisherDisplayName", "DDDuoDuo"), ("Description", "Windows developer package candidate; parity unverified" if candidate else "EndfieldHUD Windows"), ("Logo", r"Assets\StoreLogo.png")):
        ET.SubElement(properties, "{" + FOUNDATION + "}" + name).text = value
    resources = ET.SubElement(root, "{" + FOUNDATION + "}Resources")
    for language in ("en-us", "zh-cn", "zh-tw", "ja-jp", "ko-kr"):
        ET.SubElement(resources, "{" + FOUNDATION + "}Resource", {"Language": language})
    dependencies = ET.SubElement(root, "{" + FOUNDATION + "}Dependencies")
    ET.SubElement(dependencies, "{" + FOUNDATION + "}TargetDeviceFamily", {"Name": "Windows.Desktop", "MinVersion": min_os, "MaxVersionTested": min_os})
    applications = ET.SubElement(root, "{" + FOUNDATION + "}Applications")
    application = ET.SubElement(applications, "{" + FOUNDATION + "}Application", {"Id": "EndfieldHUD", "Executable": "EndfieldHUDWindows.exe", "EntryPoint": "Windows.FullTrustApplication"})
    ET.SubElement(application, "{" + UAP + "}VisualElements", {"DisplayName": "EndfieldHUD Windows preview" if candidate else "EndfieldHUD", "Description": "EndfieldHUD", "BackgroundColor": "transparent", "Square150x150Logo": r"Assets\Square150x150Logo.png", "Square44x44Logo": r"Assets\Square44x44Logo.png"})
    capabilities = ET.SubElement(root, "{" + FOUNDATION + "}Capabilities")
    ET.SubElement(capabilities, "{" + RESTRICTED + "}Capability", {"Name": "runFullTrust"})
    return ET.tostring(root, encoding="utf-8", xml_declaration=True)


def appinstaller_manifest(*, identity: str, publisher: str, version: str, package_uri: str, feed_uri: str, automatic_updates: bool = False) -> bytes:
    package_version(version)
    https_uri(package_uri)
    https_uri(feed_uri, windows_feed=True)
    ET.register_namespace("", APP_INSTALLER)
    root = ET.Element("{" + APP_INSTALLER + "}AppInstaller", {"Uri": feed_uri, "Version": version})
    ET.SubElement(root, "{" + APP_INSTALLER + "}MainPackage", {"Name": identity, "Publisher": publisher, "Version": version, "ProcessorArchitecture": "x64", "Uri": package_uri})
    if automatic_updates:
        updates = ET.SubElement(root, "{" + APP_INSTALLER + "}UpdateSettings")
        ET.SubElement(updates, "{" + APP_INSTALLER + "}OnLaunch", {"HoursBetweenUpdateChecks": "24", "ShowPrompt": "true", "UpdateBlocksActivation": "false"})
        ET.SubElement(updates, "{" + APP_INSTALLER + "}ForceUpdateFromAnyVersion").text = "false"
    return ET.tostring(root, encoding="utf-8", xml_declaration=True)


def verify_msix_payload(msix: Path, staged: Path) -> None:
    expected = {path.relative_to(staged).as_posix(): path for path in staged.rglob("*") if path.is_file()}
    generated = {"AppxBlockMap.xml", "[Content_Types].xml", "AppxSignature.p7x", "AppxMetadata/CodeIntegrity.cat"}
    with zipfile.ZipFile(msix) as package:
        names = package.namelist()
        if len(names) != len(set(names)) or package.testzip() is not None:
            raise ValueError("MSIX has duplicate entries or corrupt data")
        actual = set(names)
        if not set(expected).issubset(actual) or actual - set(expected) - generated:
            raise ValueError("MSIX payload differs from the approved runtime inventory")
        for name, source in expected.items():
            if hashlib.sha256(package.read(name)).hexdigest() != digest(source):
                raise ValueError("MSIX altered approved payload: " + name)


def build_msix(repository: Path, executable: Path, resources: Path, output: Path, *, version: str, publisher: str, logo_source: Path, min_os: str = "10.0.26200.0", candidate: bool = False, certificate_thumbprint: str | None = None, timestamp_uri: str | None = None, makeappx: Path | None = None, signtool: Path | None = None, signer_script: Path | None = None) -> dict:
    from package import authenticode, pe_architecture
    repository, executable, resources, output = repository.resolve(), executable.resolve(), resources.resolve(), output.resolve()
    if not output.is_relative_to(repository / "windows" / "dist"):
        raise ValueError("MSIX assets must remain inside windows/dist")
    pe_architecture(executable)
    inventory = verify_resources(resources)
    logo_source = logo_source.resolve()
    if not logo_source.is_relative_to(repository / "Resources"):
        raise ValueError("Installer icon must come from the approved repository runtime assets")
    logo_relative = logo_source.relative_to(repository / "Resources").as_posix()
    if logo_relative not in {record["path"] for record in inventory["files"]} or digest(logo_source) != digest(checked_file(resources, logo_relative)):
        raise ValueError("Installer icon must match a staged approved runtime asset")
    for notice in ("LICENSE.txt", "CREDITS.md", "OrbiPom/Matter-LICENSE.txt", "zlib-LICENSE.txt"):
        checked_file(resources, notice)
    source_signature = authenticode(executable)
    if not candidate:
        if source_signature.get("status") != "Valid" or source_signature.get("subject") != publisher or not certificate_thumbprint or source_signature.get("thumbprint", "").upper() != certificate_thumbprint.upper():
            raise ValueError("Trusted signed EXE, exact certificate publisher and chosen signing identity are required")
        if not timestamp_uri:
            raise ValueError("An approved RFC3161 timestamp HTTPS URL is required")
        https_uri(timestamp_uri)
    identity = PREVIEW_IDENTITY if candidate else CONSUMER_IDENTITY
    package_version(version)
    packer = sdk_tool("makeappx.exe", makeappx)
    signer = sdk_tool("signtool.exe", signtool) if not candidate else None
    output.mkdir(parents=True, exist_ok=True)
    filename = f"EndfieldHUD-Windows-x64-{'unsigned-candidate-' if candidate else ''}{version}.msix"
    destination = output / filename
    with tempfile.TemporaryDirectory(prefix=".msix-", dir=output) as temporary:
        temporary_root = Path(temporary)
        staged = temporary_root / "payload"
        staged.mkdir()
        shutil.copyfile(executable, staged / "EndfieldHUDWindows.exe")
        for record in inventory["files"]:
            target = staged / "Resources" / record["path"]
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(checked_file(resources, record["path"]), target)
        shutil.copyfile(checked_file(resources, "resources-inventory.json"), staged / "Resources" / "resources-inventory.json")
        if candidate:
            (staged / "CANDIDATE.txt").write_text("Unsigned MSIX validation candidate by DDDuoDuo. Not installable consumer release; all parity/install/update gates remain required.\n", encoding="utf-8")
        logo_script = repository / "windows" / "packaging" / "make-msix-logos.ps1"
        shell = shutil.which("pwsh.exe") or str(Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32/WindowsPowerShell/v1.0/powershell.exe")
        subprocess.run([shell, "-NoProfile", "-NonInteractive", "-File", str(logo_script), "-Source", str(logo_source.resolve()), "-Destination", str(staged / "Assets")], check=True)
        (staged / "AppxManifest.xml").write_bytes(appx_manifest(identity=identity, publisher=publisher, version=version, min_os=min_os, candidate=candidate))
        temporary_package = temporary_root / filename
        run_sdk([str(packer), "pack", "/d", windows_io_path(staged), "/p", windows_io_path(temporary_package), "/o", "/h", "SHA256"])
        verify_msix_payload(temporary_package, staged)
        if signer:
            if signer_script:
                if not signer_script.is_file() or signer_script.is_symlink() or signer_script.suffix.lower() != ".ps1":
                    raise ValueError("An explicitly chosen local signing-service PowerShell wrapper is required")
                subprocess.run([shell, "-NoProfile", "-NonInteractive", "-File", str(signer_script.resolve()), "-PackagePath", str(temporary_package), "-Publisher", publisher, "-CertificateThumbprint", certificate_thumbprint, "-TimestampUri", timestamp_uri], check=True)
            else:
                run_sdk([str(signer), "sign", "/fd", "SHA256", "/sha1", certificate_thumbprint, "/s", "My", "/tr", timestamp_uri, "/td", "SHA256", str(temporary_package)])
            run_sdk([str(signer), "verify", "/pa", "/all", "/v", str(temporary_package)])
            signature = authenticode(temporary_package)
            if signature.get("status") != "Valid" or signature.get("thumbprint", "").upper() != certificate_thumbprint.upper():
                raise ValueError("Windows did not validate the MSIX signature/chosen publisher")
            verify_msix_payload(temporary_package, staged)
        else:
            signature = {"status": "NotSigned", "reason": "explicit unsigned SDK validation candidate"}
        payload_bytes = sum(path.stat().st_size for path in staged.rglob("*") if path.is_file())
        temporary_package.replace(destination)
    return {
        "schema": 1, "kind": "unsigned-msix-candidate" if candidate else "signed-msix-release", "author": "DDDuoDuo",
        "identity": identity, "publisher": publisher, "package_version": version, "architecture": "x64",
        "minimum_os_version": min_os, "file": destination.name, "sha256": digest(destination),
        "download_bytes": destination.stat().st_size, "installed_payload_bytes": payload_bytes,
        "signature": signature, "makeappx_version": packer.parents[1].name,
        "runtime_assets": len(inventory["files"]), "created_utc": datetime.now(timezone.utc).isoformat(),
        "executable_sha256": digest(executable), "icon_source_sha256": digest(logo_source),
    }


def create_release(repository: Path, executable: Path, resources: Path, output: Path, *, windows_version: str, version: str, publisher: str, logo_source: Path, certificate_thumbprint: str, timestamp_uri: str, asset_base_uri: str, feed_uri: str, evidence_sha256: str, source_commit: str, min_os: str = "10.0.26200.0", makeappx: Path | None = None, signtool: Path | None = None, automatic_updates: bool = False, signer_script: Path | None = None) -> dict:
    https_uri(asset_base_uri, windows_feed=True)
    https_uri(feed_uri, windows_feed=True)
    report = build_msix(repository, executable, resources, output, version=version, publisher=publisher, logo_source=logo_source, min_os=min_os, certificate_thumbprint=certificate_thumbprint, timestamp_uri=timestamp_uri, makeappx=makeappx, signtool=signtool, signer_script=signer_script)
    appinstaller = output / "EndfieldHUD-Windows-x64.appinstaller"
    package_uri = asset_base_uri.rstrip("/") + "/" + report["file"]
    appinstaller.write_bytes(appinstaller_manifest(identity=CONSUMER_IDENTITY, publisher=publisher, version=version, package_uri=package_uri, feed_uri=feed_uri, automatic_updates=automatic_updates))
    report.update({"windows_version": windows_version, "source_commit": source_commit, "evidence_sha256": evidence_sha256, "appinstaller": {"file": appinstaller.name, "sha256": digest(appinstaller), "bytes": appinstaller.stat().st_size, "uri": feed_uri}, "package_uri": package_uri, "update_policy": "explicit opt-in: on-launch/24h Windows-managed updates, optional prompt, no background task" if automatic_updates else "manual update consent; automatic App Installer updates disabled", "rollback_policy": "explicit signed previous package and consent; preserve local data/snapshot"})
    manifest_path = output / "EndfieldHUD-Windows-release.json"
    manifest_path.write_bytes(json_bytes(report))
    (output / (report["file"] + ".sha256")).write_text(report["sha256"] + "  " + report["file"] + "\n", encoding="utf-8")
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate", action="store_true", help="produce a separate unsigned developer SDK-validation candidate")
    parser.add_argument("--repository", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--resources", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--publisher", default="CN=DDDuoDuo.EndfieldHUD.Windows.Development")
    parser.add_argument("--logo-source", type=Path, required=True)
    parser.add_argument("--makeappx", type=Path)
    arguments = parser.parse_args()
    if not arguments.candidate:
        parser.error("consumer release must use package.py --release with actual signature/evidence gates")
    try:
        report = build_msix(arguments.repository, arguments.executable, arguments.resources, arguments.output or arguments.repository / "windows/dist/unsigned-candidates", version="0.0.0.0", publisher=arguments.publisher, logo_source=arguments.logo_source, candidate=True, makeappx=arguments.makeappx)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + "\n")
    (arguments.output or arguments.repository / "windows/dist/unsigned-candidates").joinpath("unsigned-msix-inventory.json").write_bytes(json_bytes(report))
    print(f"Unsigned SDK-validation candidate: {report['file']}; {report['download_bytes'] / 1048576:.2f} MiB; no consumer install/release proof")


if __name__ == "__main__":
    main()
