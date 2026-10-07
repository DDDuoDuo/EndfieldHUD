#!/usr/bin/env python3
"""Create Windows preview ZIPs or evidence-gated portable/MSIX releases."""
from __future__ import annotations

import argparse
import base64
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import zipfile

from stage_resources import checked_file, json_bytes, record_file, safe_relative, verify_resources

RELEASE_GATES = (
    "transparent_source_scene_animation_tilt_hits", "backdrop_and_recordable_cursor",
    "mixed_dpi_and_monitor_hotplug", "projected_editor_ime_accessibility_fonts",
    "closed_scheduler_and_frame_pacing", "source_shader_material_parity",
    "settings_localization_and_profile", "atomic_offline_import_and_rollback",
    "notes_shelf_clipboard_archive", "reader_media_projection_map_minigame",
    "calendar_event_log_shortcuts", "battery_storage_activity_audio_work_mode",
    "china_account_login_signing_sanity", "global_account_login_signing_sanity",
    "closed_60_seconds", "open_idle_ambient_on_off", "pointer_navigation_latency",
    "cold_warm_open_close_frame_times", "heavy_modules_bounded_work",
    "100_lifecycle_cycles", "sleep_resume_device_loss_explorer_restart",
    "install_update_uninstall_consent_rollback", "architecture_and_dependency_audit",
)
MSIX_DEPLOYMENT_GATE = "install_update_uninstall_consent_rollback"
PORTABLE_DEPLOYMENT_GATE = "portable_manual_update_uninstall_consent_rollback"


def required_release_gates(distribution: str) -> tuple[str, ...]:
    if distribution not in ("portable", "msix"):
        raise ValueError("Windows distribution must be portable or msix")
    return tuple(PORTABLE_DEPLOYMENT_GATE if name == MSIX_DEPLOYMENT_GATE and distribution == "portable" else name for name in RELEASE_GATES)


def file_digest(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def pe_architecture(executable: Path) -> str:
    with executable.open("rb") as stream:
        header = stream.read(64)
        if len(header) != 64 or header[:2] != b"MZ":
            raise ValueError("Windows package requires a native PE executable")
        offset, = struct.unpack_from("<I", header, 60)
        if offset > executable.stat().st_size - 6:
            raise ValueError("Invalid PE header offset")
        stream.seek(offset)
        pe = stream.read(6)
    if pe[:4] != b"PE\0\0":
        raise ValueError("Invalid PE executable signature")
    machine, = struct.unpack_from("<H", pe, 4)
    if machine != 0x8664:
        raise ValueError("Only the Windows x64 feasibility target is supported")
    return "x64"


def git_revision(repository: Path, git: str) -> tuple[str, bool]:
    revision = subprocess.check_output([git, "-C", str(repository), "rev-parse", "HEAD"], text=True).strip()
    if not re.fullmatch(r"[0-9a-f]{40,64}", revision):
        raise ValueError("Invalid Git source revision")
    dirty = bool(subprocess.check_output([git, "-C", str(repository), "status", "--porcelain", "--untracked-files=all"], text=True).strip())
    return revision, dirty


def authenticode(executable: Path) -> dict:
    """Ask Windows to validate the real file; no supplied status can bypass it."""
    encoded_path = base64.b64encode(str(executable).encode("utf-8")).decode("ascii")
    script = f'''[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$Path = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('{encoded_path}'))
$ErrorActionPreference = 'Stop'
$signature = Get-AuthenticodeSignature -LiteralPath $Path
$certificate = $signature.SignerCertificate
[ordered]@{{
    status = [string]$signature.Status
    subject = $(if ($null -ne $certificate) {{ $certificate.Subject }} else {{ $null }})
    thumbprint = $(if ($null -ne $certificate) {{ $certificate.Thumbprint }} else {{ $null }})
}} | ConvertTo-Json -Compress
'''
    encoded_script = base64.b64encode(script.encode("utf-16-le")).decode("ascii")
    # A PowerShell 7 parent can leave its .NET modules on PSModulePath. Use
    # Windows PowerShell's own modules in this isolated validation process.
    system_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
    powershell_root = system_root / "System32" / "WindowsPowerShell" / "v1.0"
    child_environment = dict(os.environ)
    child_environment["PSModulePath"] = str(powershell_root / "Modules")
    try:
        result = subprocess.run([str(powershell_root / "powershell.exe"), "-NoProfile", "-NonInteractive", "-EncodedCommand", encoded_script], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8", errors="replace", env=child_environment)
        if result.returncode != 0:
            return {"status": "unverified", "thumbprint": None, "reason": "Windows Authenticode validation process failed"}
        return json.loads(result.stdout.lstrip("\ufeff"))
    except (OSError, ValueError):
        return {"status": "unverified", "thumbprint": None, "reason": "Windows Authenticode validation is unavailable"}


def release_failures(evidence: dict, *, version: str | None, revision: str, dirty: bool, executable_sha256: str, signature: dict, evidence_root: Path, distribution: str = "portable") -> list[str]:
    if not isinstance(evidence, dict):
        return ["Malformed Windows release evidence"]
    failures = []
    gates_required = required_release_gates(distribution)
    if not version or not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?", version):
        failures.append("DDDuoDuo must choose an explicit Windows version")
    if evidence.get("schema") != 1 or evidence.get("chosen_by") != "DDDuoDuo" or evidence.get("windows_version") != version:
        failures.append("Windows version/author approval evidence is missing or mismatched")
    if evidence.get("distribution") != distribution:
        failures.append("Chosen Windows distribution evidence is missing or mismatched")
    if evidence.get("source_commit") != revision or evidence.get("executable_sha256") != executable_sha256 or dirty:
        failures.append("Release evidence must identify this executable and a clean source commit")
    if distribution == "msix":
        if signature.get("status") != "Valid" or not signature.get("thumbprint"):
            failures.append("Executable must have a Windows-validated Authenticode signature")
        if evidence.get("signer_thumbprint") != signature.get("thumbprint"):
            failures.append("Chosen signing identity does not match the executable")
    hardware = evidence.get("hardware", {})
    if not isinstance(hardware, dict):
        hardware = {}
    if any(not hardware.get(field) for field in ("os_build", "cpu", "gpu", "ram_bytes", "monitor_dpi", "refresh_hz")):
        failures.append("Actual target laptop/build/monitor evidence is incomplete")
    gates = evidence.get("gates", {})
    if not isinstance(gates, dict):
        gates = {}
    for name in gates_required:
        gate = gates.get(name, {})
        if not isinstance(gate, dict):
            gate = {}
        if gate.get("status") != "passed" or not gate.get("summary"):
            failures.append("Unverified or failed release gate: " + name)
            continue
        artifacts = gate.get("artifacts", [])
        if not artifacts:
            failures.append("Missing measured evidence artifacts: " + name)
            continue
        for artifact in artifacts:
            try:
                path = checked_file(evidence_root, artifact["path"])
                if not re.fullmatch(r"[0-9a-f]{64}", artifact.get("sha256", "")) or file_digest(path) != artifact["sha256"]:
                    raise ValueError("mismatched digest")
            except (KeyError, TypeError, ValueError, OSError):
                failures.append("Missing or altered evidence artifact for: " + name)
    return failures


def package_preview(repository: Path, executable: Path, resources: Path, output: Path, *, git: str, build_metadata: Path | None = None, release: bool = False, version: str | None = None, evidence_path: Path | None = None, release_options: dict | None = None, distribution: str = "portable") -> dict:
    repository, executable, resources, output = repository.resolve(), executable.resolve(), resources.resolve(), output.resolve()
    if not output.is_relative_to(repository / "windows" / "dist"):
        raise ValueError("Windows packages must stay inside windows/dist, separate from Mac releases")
    architecture = pe_architecture(executable)
    resource_inventory = verify_resources(resources)
    for notice in ("LICENSE.txt", "CREDITS.md", "OrbiPom/Matter-LICENSE.txt", "zlib-LICENSE.txt"):
        checked_file(resources, notice)
    revision, dirty = git_revision(repository, git)
    executable_hash = file_digest(executable)
    signature = authenticode(executable)
    required_release_gates(distribution)
    if release:
        evidence = json.loads(evidence_path.read_bytes()) if evidence_path else {}
        failures = release_failures(evidence, version=version, revision=revision, dirty=dirty, executable_sha256=executable_hash, signature=signature, evidence_root=evidence_path.parent if evidence_path else repository, distribution=distribution)
        options = release_options or {}
        if distribution == "msix":
            for field in ("package_version", "publisher", "logo_source", "certificate_thumbprint", "timestamp_uri", "asset_base_uri", "feed_uri"):
                if not options.get(field):
                    failures.append("Missing chosen signed MSIX release setting: " + field)
        if failures:
            raise ValueError("Consumer Windows release refused:\n- " + "\n- ".join(failures))
        if distribution == "msix":
            from msix_release import create_release
            return create_release(repository, executable, resources, output, windows_version=version, version=options["package_version"], publisher=options["publisher"], logo_source=options["logo_source"], certificate_thumbprint=options["certificate_thumbprint"], timestamp_uri=options["timestamp_uri"], asset_base_uri=options["asset_base_uri"], feed_uri=options["feed_uri"], evidence_sha256=file_digest(evidence_path), source_commit=revision, min_os=options.get("min_os", "10.0.26200.0"), makeappx=options.get("makeappx"), signtool=options.get("signtool"), automatic_updates=bool(options.get("automatic_updates")), signer_script=options.get("signer_script"))
    if not release and (version or evidence_path):
        raise ValueError("Developer preview must not assign a consumer Windows version")
    files: dict[str, Path] = {"EndfieldHUDWindows.exe": executable}
    for record in resource_inventory["files"]:
        files["Resources/" + record["path"]] = checked_file(resources, record["path"])
    files["Resources/resources-inventory.json"] = checked_file(resources, "resources-inventory.json")
    if release:
        for helper in ("portable-update.ps1", "data-snapshots.ps1"):
            files[helper] = checked_file(Path(__file__).resolve().parent, helper)
    if build_metadata:
        metadata = json.loads(build_metadata.read_bytes())
    else:
        metadata = {"status": "unverified", "reason": "build metadata not supplied"}
    preview_notice = (
        "EndfieldHUD Windows feasibility developer preview\n"
        "Authorship: DDDuoDuo\n"
        "This is an incomplete native Windows prototype, not a consumer release.\n"
        "Full visual/module/data/IME/accessibility/performance/install/update parity is unverified.\n"
        "Use isolated synthetic data. No Mac release or update feed is changed.\n"
        "See Resources/LICENSE.txt and Resources/CREDITS.md for ownership and notices.\n"
    ).encode("utf-8")
    notice_name = "PORTABLE.txt" if release else "PREVIEW.txt"
    if release:
        preview_notice = (
            f"EndfieldHUD Windows portable release {version}\n"
            "Authorship: DDDuoDuo\n"
            "Portable ZIP distribution. Authenticode signing is not required.\n"
            f"Observed Windows Authenticode status: {signature.get('status', 'unverified')}\n"
            "Verify the published ZIP SHA-256 before extraction or a manual update.\n"
            "Windows can show an unknown-publisher warning for an unsigned executable.\n"
            "Save and quit before updating. Keep app data outside this executable/resource folder.\n"
            "portable-update.ps1 validates every payload, preserves the previous version and snapshots data.\n"
            "Updates and rollback require explicit user consent; no updater runs in the background.\n"
            "See Resources/LICENSE.txt and Resources/CREDITS.md for ownership and notices.\n"
        ).encode("utf-8")
    records = [record_file(path, path.parent, path_in_package=name) for name, path in sorted(files.items())]
    for record in records:
        record["path"] = record.pop("path_in_package")
    records.append({"path": notice_name, "bytes": len(preview_notice), "sha256": hashlib.sha256(preview_notice).hexdigest()})
    inventory = {
        "schema": 1, "kind": "portable-consumer-release" if release else "developer-preview", "author": "DDDuoDuo", "platform": "Windows",
        "distribution": "portable", "architecture": architecture, "windows_version": version if release else None, "source_commit": revision, "source_dirty": dirty,
        "executable_sha256": executable_hash, "authenticode": signature, "build": metadata,
        "authenticode_required": False,
        "update_policy": "manual consent; closed app; verified hashes; retain previous payload; optional explicit data rollback",
        "runtime_dependencies": [
            {"name": "Windows system APIs", "distribution": "OS provided", "package_bytes": 0, "license": "Windows license", "version": "see build SDK and measured OS evidence"},
            {"name": "MSVC C++ runtime", "distribution": "statically linked /MT", "package_bytes": "included in executable", "license": "Microsoft Visual Studio runtime redistribution terms", "version": "see build compiler metadata"},
            {"name": "Matter.js", "version": "0.20.0", "distribution": "original OrbiPom asset", "license": "MIT", "license_path": "Resources/OrbiPom/Matter-LICENSE.txt", "engine_status": "see accepted native game evidence" if release else "Windows game adapter parity is unverified"},
            {"name": "zlib", "version": "1.3.2", "archive_sha256": "bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16", "distribution": "six source files statically linked; no DLL", "license": "zlib", "license_path": "Resources/zlib-LICENSE.txt", "package_bytes": "included in measured executable; 1002-byte notice"},
        ],
        "files": records, "installed_payload_bytes": sum(record["bytes"] for record in records),
        "resources_bytes": resource_inventory["installed_bytes"],
        "native_scene_duplicate_bytes": resource_inventory["native_scene_duplicate_bytes"],
        "consumer_release": "all applicable acceptance evidence passed" if release else "requires completed feasibility/parity/data/performance and portable deployment evidence; signing is optional for portable ZIP",
    }
    if release:
        inventory["release_evidence_sha256"] = file_digest(evidence_path)
        inventory["passed_release_gates"] = list(required_release_gates("portable"))
    output.mkdir(parents=True, exist_ok=True)
    stem = f"EndfieldHUD-Windows-{architecture}-portable-{version}" if release else f"EndfieldHUD-Windows-{architecture}-preview-{revision[:12]}"
    archive = output / (stem + ".zip")
    with tempfile.TemporaryDirectory(prefix=".windows-package-", dir=output) as temporary:
        staged_archive = Path(temporary) / archive.name
        with zipfile.ZipFile(staged_archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as package:
            payloads = {name: path.read_bytes() for name, path in files.items()}
            payloads[notice_name] = preview_notice
            payloads["package-inventory.json"] = json_bytes(inventory)
            for name, data in sorted(payloads.items()):
                safe_relative(name)
                info = zipfile.ZipInfo("EndfieldHUD-Windows/" + name, date_time=(1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o644 << 16
                package.writestr(info, data, compress_type=zipfile.ZIP_DEFLATED, compresslevel=6)
        with zipfile.ZipFile(staged_archive) as package:
            if package.testzip() is not None:
                raise ValueError("Windows preview ZIP failed its integrity check")
            for record in records:
                data = package.read("EndfieldHUD-Windows/" + record["path"])
                if len(data) != record["bytes"] or hashlib.sha256(data).hexdigest() != record["sha256"]:
                    raise ValueError("Windows ZIP payload differs from its inventory")
        staged_archive.replace(archive)
    report = {
        **inventory, "archive": archive.name, "download_bytes": archive.stat().st_size,
        "archive_sha256": file_digest(archive), "package_inventory_bytes": len(json_bytes(inventory)),
        "installed_bytes_including_inventory": inventory["installed_payload_bytes"] + len(json_bytes(inventory)),
        "created_utc": datetime.now(timezone.utc).isoformat(),
    }
    (output / (stem + "-inventory.json")).write_bytes(json_bytes(report))
    (output / (stem + ".zip.sha256")).write_text(report["archive_sha256"] + "  " + archive.name + "\n", encoding="utf-8")
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--resources", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--git", default="git")
    parser.add_argument("--build-metadata", type=Path)
    parser.add_argument("--release", action="store_true")
    parser.add_argument("--format", choices=("portable", "msix"), default="portable", dest="distribution", help="portable ZIP requires no signing certificate; msix is an optional signed distribution")
    parser.add_argument("--version")
    parser.add_argument("--evidence", type=Path)
    parser.add_argument("--package-version")
    parser.add_argument("--publisher")
    parser.add_argument("--logo-source", type=Path)
    parser.add_argument("--certificate-thumbprint")
    parser.add_argument("--timestamp-uri")
    parser.add_argument("--asset-base-uri")
    parser.add_argument("--feed-uri")
    parser.add_argument("--min-os", default="10.0.26200.0")
    parser.add_argument("--makeappx", type=Path)
    parser.add_argument("--signtool", type=Path)
    parser.add_argument("--automatic-updates", action="store_true", help="explicitly opt into Windows App Installer launch checks; default is manual consent")
    parser.add_argument("--signer-script", type=Path, help="explicit local PowerShell wrapper for an authorized signing service; output is still trust/hash verified")
    arguments = parser.parse_args()
    try:
        options = {"package_version": arguments.package_version, "publisher": arguments.publisher, "logo_source": arguments.logo_source, "certificate_thumbprint": arguments.certificate_thumbprint, "timestamp_uri": arguments.timestamp_uri, "asset_base_uri": arguments.asset_base_uri, "feed_uri": arguments.feed_uri, "min_os": arguments.min_os, "makeappx": arguments.makeappx, "signtool": arguments.signtool, "automatic_updates": arguments.automatic_updates, "signer_script": arguments.signer_script}
        report = package_preview(arguments.repository, arguments.executable, arguments.resources, arguments.output or arguments.repository / "windows" / "dist", git=arguments.git, build_metadata=arguments.build_metadata, release=arguments.release, version=arguments.version, evidence_path=arguments.evidence, release_options=options, distribution=arguments.distribution)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + "\n")
    if arguments.release and arguments.distribution == "msix":
        print(f"Signed Windows MSIX: {report['file']}; download {report['download_bytes'] / 1048576:.2f} MiB; installed payload {report['installed_payload_bytes'] / 1048576:.2f} MiB")
        print(f"SHA-256: {report['sha256']}")
    else:
        label = "Windows portable release" if arguments.release else "Windows developer preview"
        print(f"{label}: {report['archive']}; download {report['download_bytes'] / 1048576:.2f} MiB; installed {report['installed_bytes_including_inventory'] / 1048576:.2f} MiB")
        print(f"SHA-256: {report['archive_sha256']}")


if __name__ == "__main__":
    main()
