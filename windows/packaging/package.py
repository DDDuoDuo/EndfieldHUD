#!/usr/bin/env python3
"""Create an isolated Windows developer preview and report consumer release gaps."""
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


def release_failures(evidence: dict, *, version: str | None, revision: str, dirty: bool, executable_sha256: str, signature: dict, evidence_root: Path) -> list[str]:
    if not isinstance(evidence, dict):
        return ["Malformed Windows release evidence"]
    failures = []
    if not version or not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?", version):
        failures.append("DDDuoDuo must choose an explicit Windows version")
    if evidence.get("schema") != 1 or evidence.get("chosen_by") != "DDDuoDuo" or evidence.get("windows_version") != version:
        failures.append("Windows version/author approval evidence is missing or mismatched")
    if evidence.get("source_commit") != revision or evidence.get("executable_sha256") != executable_sha256 or dirty:
        failures.append("Release evidence must identify this executable and a clean source commit")
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
    for name in RELEASE_GATES:
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


def package_preview(repository: Path, executable: Path, resources: Path, output: Path, *, git: str, build_metadata: Path | None = None, release: bool = False, version: str | None = None, evidence_path: Path | None = None) -> dict:
    repository, executable, resources, output = repository.resolve(), executable.resolve(), resources.resolve(), output.resolve()
    if not output.is_relative_to(repository / "windows" / "dist"):
        raise ValueError("Windows packages must stay inside windows/dist, separate from Mac releases")
    architecture = pe_architecture(executable)
    resource_inventory = verify_resources(resources)
    for notice in ("LICENSE.txt", "CREDITS.md", "OrbiPom/Matter-LICENSE.txt"):
        checked_file(resources, notice)
    revision, dirty = git_revision(repository, git)
    executable_hash = file_digest(executable)
    signature = authenticode(executable)
    if release:
        evidence = json.loads(evidence_path.read_bytes()) if evidence_path else {}
        failures = release_failures(evidence, version=version, revision=revision, dirty=dirty, executable_sha256=executable_hash, signature=signature, evidence_root=evidence_path.parent if evidence_path else repository)
        # A ZIP preview does not prove desktop installation, update replacement,
        # rollback or signed installer capabilities. Deliberately refuse until
        # the chosen consumer installer pipeline is implemented and tested.
        failures.append("Consumer installer/updater is not implemented; this pipeline creates developer previews only")
        raise ValueError("Consumer Windows release refused:\n- " + "\n- ".join(failures))
    if version or evidence_path:
        raise ValueError("Developer preview must not assign a consumer Windows version")
    files: dict[str, Path] = {"EndfieldHUDWindows.exe": executable}
    for record in resource_inventory["files"]:
        files["Resources/" + record["path"]] = checked_file(resources, record["path"])
    files["Resources/resources-inventory.json"] = checked_file(resources, "resources-inventory.json")
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
    records = [record_file(path, path.parent, path_in_package=name) for name, path in sorted(files.items())]
    for record in records:
        record["path"] = record.pop("path_in_package")
    records.append({"path": "PREVIEW.txt", "bytes": len(preview_notice), "sha256": hashlib.sha256(preview_notice).hexdigest()})
    inventory = {
        "schema": 1, "kind": "developer-preview", "author": "DDDuoDuo", "platform": "Windows",
        "architecture": architecture, "windows_version": None, "source_commit": revision, "source_dirty": dirty,
        "executable_sha256": executable_hash, "authenticode": signature, "build": metadata,
        "runtime_dependencies": [
            {"name": "Windows system APIs", "distribution": "OS provided", "package_bytes": 0, "license": "Windows license", "version": "see build SDK and measured OS evidence"},
            {"name": "MSVC C++ runtime", "distribution": "statically linked /MT", "package_bytes": "included in executable", "license": "Microsoft Visual Studio runtime redistribution terms", "version": "see build compiler metadata"},
            {"name": "Matter.js", "version": "0.20.0", "distribution": "dormant original OrbiPom asset", "license": "MIT", "license_path": "Resources/OrbiPom/Matter-LICENSE.txt", "engine_status": "Windows game adapter not implemented"},
        ],
        "files": records, "installed_payload_bytes": sum(record["bytes"] for record in records),
        "resources_bytes": resource_inventory["installed_bytes"],
        "native_scene_duplicate_bytes": resource_inventory["native_scene_duplicate_bytes"],
        "consumer_release": "blocked by incomplete feasibility/parity evidence and unimplemented signed installer/updater",
    }
    output.mkdir(parents=True, exist_ok=True)
    stem = f"EndfieldHUD-Windows-{architecture}-preview-{revision[:12]}"
    archive = output / (stem + ".zip")
    with tempfile.TemporaryDirectory(prefix=".windows-package-", dir=output) as temporary:
        staged_archive = Path(temporary) / archive.name
        with zipfile.ZipFile(staged_archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as package:
            payloads = {name: path.read_bytes() for name, path in files.items()}
            payloads["PREVIEW.txt"] = preview_notice
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
    parser.add_argument("--version")
    parser.add_argument("--evidence", type=Path)
    arguments = parser.parse_args()
    try:
        report = package_preview(arguments.repository, arguments.executable, arguments.resources, arguments.output or arguments.repository / "windows" / "dist", git=arguments.git, build_metadata=arguments.build_metadata, release=arguments.release, version=arguments.version, evidence_path=arguments.evidence)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + "\n")
    print(f"Windows developer preview: {report['archive']}; download {report['download_bytes'] / 1048576:.2f} MiB; installed {report['installed_bytes_including_inventory'] / 1048576:.2f} MiB")
    print(f"SHA-256: {report['archive_sha256']}")


if __name__ == "__main__":
    main()
