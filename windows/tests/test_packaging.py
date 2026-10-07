"""Synthetic format, asset-boundary and release-gate checks; no user data."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import shutil
import sys
import tempfile
import unittest
from unittest import mock
import zipfile
import xml.etree.ElementTree as ET
import zlib

PACKAGING = Path(__file__).resolve().parents[1] / "packaging"
sys.path.insert(0, str(PACKAGING))
import package as windows_package
import stage_resources
import msix_release


def container(data: bytes, length: int | None = None) -> bytes:
    compressor = zlib.compressobj(wbits=-15)
    return stage_resources.MAGIC + struct.pack("<Q", len(data) if length is None else length) + compressor.compress(data) + compressor.flush()


class ResourceFormatTests(unittest.TestCase):
    def test_round_trip_preserves_tokens_and_identifiers(self):
        data = b'{"id":"-9088776655443322110","value":0.123456789012345678901,"text":"\\ud83d\\ude00"}'
        self.assertEqual(stage_resources.decode_resource(container(data)), data)

    def test_identity_is_unchanged(self):
        self.assertEqual(stage_resources.decode_resource(b"\x89PNG\r\n\x1a\n"), b"\x89PNG\r\n\x1a\n")

    def test_truncated_headers_rejected(self):
        for size in range(8, 16):
            with self.subTest(size=size), self.assertRaises(ValueError):
                stage_resources.decode_resource((stage_resources.MAGIC + b"\0" * 8)[:size])

    def test_incorrect_magic_rejected_when_container_expected(self):
        with self.assertRaises(ValueError):
            stage_resources.decode_resource(b"EHUDZ02\0" + b"\0" * 16, require_container=True)

    def test_zero_length_rejected(self):
        with self.assertRaises(ValueError):
            stage_resources.decode_resource(container(b"", length=0))

    def test_above_128_mib_rejected_before_decompression(self):
        with self.assertRaisesRegex(ValueError, "uncompressed length"):
            stage_resources.decode_resource(container(b"x", length=stage_resources.MAX_RESOURCE_BYTES + 1))

    def test_64_bit_length_cannot_wrap(self):
        with self.assertRaisesRegex(ValueError, "uncompressed length"):
            stage_resources.decode_resource(container(b"x", length=(1 << 64) - 1))

    def test_exact_128_mib_bound_is_permitted(self):
        # Build the boundary stream incrementally, without a giant source image.
        compressor = zlib.compressobj(wbits=-15)
        chunks = [compressor.compress(b"x" * 1048576) for _ in range(128)]
        payload = stage_resources.MAGIC + struct.pack("<Q", stage_resources.MAX_RESOURCE_BYTES) + b"".join(chunks) + compressor.flush()
        decoded = stage_resources.decode_resource(payload)
        self.assertEqual(len(decoded), stage_resources.MAX_RESOURCE_BYTES)
        self.assertEqual(decoded[-1:], b"x")

    def test_malformed_deflate_rejected(self):
        with self.assertRaisesRegex(ValueError, "raw-DEFLATE"):
            stage_resources.decode_resource(stage_resources.MAGIC + struct.pack("<Q", 1) + b"\x07")

    def test_truncated_stream_rejected(self):
        with self.assertRaises(ValueError):
            stage_resources.decode_resource(container(b"payload" * 100)[:-1])

    def test_trailing_bytes_rejected(self):
        with self.assertRaises(ValueError):
            stage_resources.decode_resource(container(b"payload") + b"unexpected")

    def test_second_concatenated_stream_rejected(self):
        with self.assertRaises(ValueError):
            stage_resources.decode_resource(container(b"payload") + container(b"extra")[16:])

    def test_declared_length_must_match_exactly(self):
        for length in (1, 8):
            with self.subTest(length=length), self.assertRaises(ValueError):
                stage_resources.decode_resource(container(b"payload", length=length))

    def test_high_compression_bomb_is_bounded_by_header(self):
        with self.assertRaises(ValueError):
            stage_resources.decode_resource(container(b"x" * (1024 * 1024), length=1))


class AssetBoundaryTests(unittest.TestCase):
    def test_windows_and_parent_paths_are_rejected(self):
        for name in ("../real-user-data", "/absolute", "C:/Users/person", "a\\b", "./file", "a/../file"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                stage_resources.safe_relative(name)

    def test_only_explicit_files_are_selected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "Watch").mkdir()
            (root / "Watch" / "approved.png").write_bytes(b"approved")
            (root / "Watch" / "reference.gif").write_bytes(b"private recording")
            selection = {"schema": 1, "watch_profile": "desktop", "files": [], "trees": [{"source": "Watch", "patterns": ["*.png"]}]}
            self.assertEqual([name for name, _ in stage_resources.source_assets(root, selection)], ["Watch/approved.png"])

    def test_faction_atlas_is_excluded(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "icons").mkdir()
            for name in ("FactionAtlas.png", "Perlica.png"):
                (root / "icons" / name).write_bytes(b"pixels")
            selection = {"schema": 1, "watch_profile": "desktop", "files": [], "trees": [{"source": "icons", "patterns": ["*.png"], "exclude": ["FactionAtlas.png"]}]}
            self.assertEqual([name for name, _ in stage_resources.source_assets(root, selection)], ["icons/Perlica.png"])

    def test_missing_asset_is_reported(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, "Missing"):
                stage_resources.checked_file(Path(directory), "missing.png")

    def test_only_x64_native_executables_are_permitted(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "app.exe"
            data = bytearray(70)
            data[:2] = b"MZ"
            struct.pack_into("<I", data, 60, 64)
            data[64:68] = b"PE\0\0"
            struct.pack_into("<H", data, 68, 0x8664)
            path.write_bytes(data)
            self.assertEqual(windows_package.pe_architecture(path), "x64")
            struct.pack_into("<H", data, 68, 0xAA64)
            path.write_bytes(data)
            with self.assertRaisesRegex(ValueError, "x64"):
                windows_package.pe_architecture(path)

    def test_modified_pinned_dependency_is_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "engine.js").write_bytes(b"changed library")
            (root / "LICENSE.txt").write_bytes(b"synthetic license")
            selection = {
                "schema": 1, "watch_profile": "desktop", "files": ["engine.js", "LICENSE.txt"], "trees": [],
                "pins": [{"path": "engine.js", "license_path": "LICENSE.txt", "bytes": 1, "sha256": "a" * 64, "license_sha256": hashlib.sha256(b"synthetic license").hexdigest()}],
            }
            with self.assertRaisesRegex(ValueError, "Pinned"):
                stage_resources.source_assets(root, selection)


class WindowsStagingLockTests(unittest.TestCase):
    def test_temporary_windows_lock_is_retried(self):
        locked = subprocess.CompletedProcess(["packer"], 1, "", "PermissionError: [WinError 5] Access denied")
        success = subprocess.CompletedProcess(["packer"], 0, "verified\n", "")
        with mock.patch.object(stage_resources.os, "name", "nt"), mock.patch.object(stage_resources.subprocess, "run", side_effect=[locked, success]) as run, mock.patch.object(stage_resources.time, "sleep"), mock.patch("builtins.print"):
            stage_resources.run_watch_packer(["synthetic-packer"])
        self.assertEqual(run.call_count, 2)

    def test_non_lock_failure_is_not_retried(self):
        failed = subprocess.CompletedProcess(["packer"], 1, "", "ValueError: source inventory differs")
        with mock.patch.object(stage_resources.subprocess, "run", return_value=failed) as run, mock.patch("builtins.print"):
            with self.assertRaises(subprocess.CalledProcessError):
                stage_resources.run_watch_packer(["synthetic-packer"])
        self.assertEqual(run.call_count, 1)

    def test_directory_promotion_retries_scan_lock(self):
        source = mock.Mock()
        error = PermissionError("synthetic Windows scan lock")
        error.winerror = 5
        source.rename.side_effect = [error, None]
        with mock.patch.object(stage_resources.time, "sleep"):
            stage_resources.rename_directory(source, mock.Mock())
        self.assertEqual(source.rename.call_count, 2)


class MsixManifestTests(unittest.TestCase):
    def test_msix_version_bounds(self):
        self.assertEqual(msix_release.package_version("1.2.3.65535"), "1.2.3.65535")
        for invalid in ("1.2.3", "1.2.3.65536", "01.2.3.4", "-1.2.3.4", "1.2.3.beta"):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                msix_release.package_version(invalid)

    def test_unsigned_candidate_has_separate_identity(self):
        data = msix_release.appx_manifest(identity=msix_release.PREVIEW_IDENTITY, publisher="CN=Synthetic", version="0.0.0.0", min_os="10.0.26200.0", candidate=True)
        document = ET.fromstring(data)
        identity = document.find("{" + msix_release.FOUNDATION + "}Identity")
        self.assertEqual(identity.attrib["Name"], msix_release.PREVIEW_IDENTITY)
        self.assertEqual(identity.attrib["ProcessorArchitecture"], "x64")
        self.assertIn(b"Windows.FullTrustApplication", data)

    def test_default_appinstaller_requires_manual_updates(self):
        data = msix_release.appinstaller_manifest(identity=msix_release.CONSUMER_IDENTITY, publisher="CN=Synthetic", version="1.2.3.4", package_uri="https://example.test/windows/package.msix", feed_uri="https://example.test/windows/EndfieldHUD-Windows.appinstaller")
        self.assertNotIn(b"UpdateSettings", data)
        self.assertNotIn(b"AutomaticBackgroundTask", data)

    def test_opt_in_appinstaller_has_bounded_launch_checks(self):
        data = msix_release.appinstaller_manifest(identity=msix_release.CONSUMER_IDENTITY, publisher="CN=Synthetic", version="1.2.3.4", package_uri="https://example.test/windows/package.msix", feed_uri="https://example.test/windows/EndfieldHUD-Windows.appinstaller", automatic_updates=True)
        document = ET.fromstring(data)
        launch = document.find("{" + msix_release.APP_INSTALLER + "}UpdateSettings/{" + msix_release.APP_INSTALLER + "}OnLaunch")
        self.assertEqual(launch.attrib, {"HoursBetweenUpdateChecks": "24", "ShowPrompt": "true", "UpdateBlocksActivation": "false"})
        self.assertNotIn(b"AutomaticBackgroundTask", data)

    def test_mac_latest_and_insecure_feeds_are_rejected(self):
        for uri in ("http://example.test/windows/feed", "https://user:secret@example.test/windows/feed", "https://example.test/mac/feed", "https://github.com/person/repo/releases/latest/download/EndfieldHUD-Windows.appinstaller"):
            with self.subTest(uri=uri), self.assertRaises(ValueError):
                msix_release.https_uri(uri, windows_feed=True)

    def test_msix_payload_is_byte_exact_and_rejects_extra_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            staged = root / "payload"
            staged.mkdir()
            (staged / "LICENSE.txt").write_bytes(b"synthetic license")
            archive = root / "synthetic.msix"
            with zipfile.ZipFile(archive, "w") as package:
                package.writestr("LICENSE.txt", b"synthetic license")
                package.writestr("AppxBlockMap.xml", b"synthetic SDK metadata")
            msix_release.verify_msix_payload(archive, staged)
            with zipfile.ZipFile(archive, "a") as package:
                package.writestr("private-recording.gif", b"synthetic forbidden content")
            with self.assertRaisesRegex(ValueError, "approved runtime"):
                msix_release.verify_msix_payload(archive, staged)


@unittest.skipUnless(os.name == "nt", "Windows PowerShell snapshot helper integration")
class InstallerSnapshotTests(unittest.TestCase):
    def run_snapshot_case(self, *, corrupt: bool, hidden: bool = False):
        with tempfile.TemporaryDirectory(prefix="ehud-synthetic-update-") as directory:
            root = Path(directory)
            script = root / "snapshot-test.ps1"
            script.write_text('''param([string]$Helper,[string]$Root,[string]$Corrupt,[string]$Hidden)
$ErrorActionPreference='Stop'
$identityName='DDDuoDuo.EndfieldHUD.Windows'
$errors=$null;$tokens=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($Helper,[ref]$tokens,[ref]$errors)
if($errors.Count -gt 0){throw 'Updater parse failure'}
foreach($statement in $ast.EndBlock.Statements){if($statement -is [Management.Automation.Language.FunctionDefinitionAst]){. ([scriptblock]::Create($statement.Extent.Text))}}
$data=Join-Path $Root 'SyntheticData'
$backups=Join-Path $Root 'SyntheticBackups'
if($Hidden -eq 'yes'){[IO.File]::SetAttributes($Root,[IO.File]::GetAttributes($Root) -bor [IO.FileAttributes]::Hidden)}
$data=Get-CanonicalDirectory $data
$backups=Get-CanonicalDirectory $backups
New-Item -ItemType Directory -Path $data | Out-Null
[IO.File]::WriteAllText((Join-Path $data 'synthetic-note.json'),'original synthetic data')
if($Hidden -eq 'yes'){
    [IO.File]::SetAttributes($data,[IO.File]::GetAttributes($data) -bor [IO.FileAttributes]::Hidden)
    $hiddenFile=Join-Path $data 'hidden-synthetic-note.json'
    [IO.File]::WriteAllText($hiddenFile,'original hidden synthetic data')
    [IO.File]::SetAttributes($hiddenFile,[IO.File]::GetAttributes($hiddenFile) -bor [IO.FileAttributes]::Hidden)
}
$snapshot=New-DataSnapshot $data $backups '1.0.0.0'
[IO.File]::WriteAllText((Join-Path $data 'synthetic-note.json'),'new synthetic data')
if($Hidden -eq 'yes'){
    $changedBytes=[Text.Encoding]::UTF8.GetBytes('new hidden synthetic data')
    $changedFile=[IO.File]::Open($hiddenFile,[IO.FileMode]::Open,[IO.FileAccess]::Write)
    try{$changedFile.SetLength(0);$changedFile.Write($changedBytes,0,$changedBytes.Length)}finally{$changedFile.Dispose()}
}
$rejected=$false
$rejectionReason=$null
if($Corrupt -eq 'yes'){[IO.File]::WriteAllText((Join-Path $snapshot 'data/synthetic-note.json'),'corrupted snapshot')}
try{Restore-VerifiedSnapshot $snapshot $data $backups '1.0.0.0'}catch{if($Corrupt -ne 'yes'){throw};$rejected=$true;$rejectionReason=$_.Exception.Message}
$hiddenContent=if($Hidden -eq 'yes'){[IO.File]::ReadAllText((Join-Path $data 'hidden-synthetic-note.json'))}else{$null}
[ordered]@{rejected=$rejected;rejection_reason=$rejectionReason;content=[IO.File]::ReadAllText((Join-Path $data 'synthetic-note.json'));hidden_content=$hiddenContent;snapshot_exists=(Test-Path -LiteralPath $snapshot)} | ConvertTo-Json -Compress
''', encoding="utf-8")
            shell = shutil.which("pwsh.exe") or "powershell.exe"
            result = subprocess.run([shell, "-NoProfile", "-NonInteractive", "-File", str(script), "-Helper", str(PACKAGING / "install-update.ps1"), "-Root", str(root), "-Corrupt", "yes" if corrupt else "no", "-Hidden", "yes" if hidden else "no"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, encoding="utf-8")
            self.assertEqual(result.returncode, 0, result.stderr)
            return json.loads(result.stdout)

    def test_synthetic_snapshot_restores_exact_original_data(self):
        result = self.run_snapshot_case(corrupt=False)
        self.assertEqual(result["content"], "original synthetic data")
        self.assertTrue(result["snapshot_exists"])

    def test_altered_snapshot_does_not_replace_current_data(self):
        result = self.run_snapshot_case(corrupt=True)
        self.assertTrue(result["rejected"])
        self.assertEqual(result["rejection_reason"], "Rollback snapshot hash/length differs.")
        self.assertEqual(result["content"], "new synthetic data")

    def test_hidden_ancestors_and_files_restore_exact_original_data(self):
        result = self.run_snapshot_case(corrupt=False, hidden=True)
        self.assertEqual(result["content"], "original synthetic data")
        self.assertEqual(result["hidden_content"], "original hidden synthetic data")
        self.assertTrue(result["snapshot_exists"])


class ReleaseGateTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        artifact = self.root / "synthetic-measurement.json"
        artifact.write_bytes(b'{"synthetic":true}')
        self.digest = hashlib.sha256(artifact.read_bytes()).hexdigest()
        self.evidence = {
            "schema": 1, "chosen_by": "DDDuoDuo", "windows_version": "9.9.9",
            "source_commit": "a" * 40, "executable_sha256": "b" * 64, "signer_thumbprint": "TEST",
            "hardware": {"os_build": "synthetic", "cpu": "synthetic", "gpu": "synthetic", "ram_bytes": 1, "monitor_dpi": 96, "refresh_hz": 60},
            "gates": {name: {"status": "passed", "summary": "synthetic validator test", "artifacts": [{"path": artifact.name, "sha256": self.digest}]} for name in windows_package.RELEASE_GATES},
        }

    def failures(self, **overrides):
        arguments = {"version": "9.9.9", "revision": "a" * 40, "dirty": False, "executable_sha256": "b" * 64, "signature": {"status": "Valid", "thumbprint": "TEST"}, "evidence_root": self.root}
        arguments.update(overrides)
        return windows_package.release_failures(self.evidence, **arguments)

    def test_complete_synthetic_evidence_has_no_validator_errors(self):
        self.assertEqual(self.failures(), [])

    def test_unverified_feasibility_blocks_release(self):
        self.evidence["gates"][windows_package.RELEASE_GATES[0]]["status"] = "unverified"
        self.assertTrue(any("Unverified" in failure for failure in self.failures()))

    def test_chosen_windows_version_is_required(self):
        self.assertTrue(any("choose" in failure for failure in self.failures(version=None)))

    def test_signature_must_be_valid(self):
        self.assertTrue(any("Authenticode" in failure for failure in self.failures(signature={"status": "NotSigned", "thumbprint": None})))

    def test_dirty_or_different_build_cannot_reuse_evidence(self):
        self.assertTrue(any("clean source" in failure for failure in self.failures(dirty=True)))
        self.assertTrue(any("clean source" in failure for failure in self.failures(executable_sha256="c" * 64)))

    def test_evidence_artifact_hash_is_verified(self):
        (self.root / "synthetic-measurement.json").write_bytes(b"changed")
        self.assertTrue(any("altered" in failure for failure in self.failures()))

    def test_evidence_artifact_cannot_escape_root(self):
        self.evidence["gates"][windows_package.RELEASE_GATES[0]]["artifacts"][0]["path"] = "../private"
        self.assertTrue(any("altered" in failure for failure in self.failures()))

    def test_malformed_evidence_is_rejected(self):
        self.evidence["gates"] = {windows_package.RELEASE_GATES[0]: None}
        self.evidence["hardware"] = None
        self.assertTrue(self.failures())

    def test_template_has_every_gate_and_passes_none(self):
        template = json.loads((PACKAGING / "release-evidence.template.json").read_bytes())
        self.assertEqual(set(template["gates"]), set(windows_package.RELEASE_GATES))
        self.assertTrue(all(gate["status"] == "unverified" for gate in template["gates"].values()))


class PreviewPackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.repository = Path(self.temporary.name)
        release_root = self.repository / "windows" / "build" / "x64" / "Release"
        self.resources = release_root / "Resources"
        self.resources.mkdir(parents=True)
        for name, data in {
            "LICENSE.txt": b"synthetic MIT notice",
            "CREDITS.md": b"synthetic credits",
            "OrbiPom/Matter-LICENSE.txt": b"synthetic dependency notice",
            "zlib-LICENSE.txt": b"synthetic zlib notice",
            "WatchSource/runtime-inventory.json": stage_resources.json_bytes({"schema": 1, "profile": "desktop", "files": [], "derived_files": []}),
        }.items():
            path = self.resources / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        files = [stage_resources.record_file(path, self.resources) for path in self.resources.rglob("*") if path.is_file()]
        inventory = {"schema": 1, "policy": "explicit-runtime-assets-only", "files": files, "installed_bytes": sum(record["bytes"] for record in files), "native_scene_duplicate_bytes": 0}
        (self.resources / "resources-inventory.json").write_bytes(stage_resources.json_bytes(inventory))
        self.executable = release_root / "EndfieldHUDWindows.exe"
        data = bytearray(70)
        data[:2] = b"MZ"
        struct.pack_into("<I", data, 60, 64)
        data[64:68] = b"PE\0\0"
        struct.pack_into("<H", data, 68, 0x8664)
        self.executable.write_bytes(data)
        (release_root / "EndfieldHUDWindows.pdb").write_bytes(b"synthetic developer symbols")
        (release_root / "private-user-data.json").write_bytes(b"synthetic forbidden data")

    def test_preview_reports_sizes_hashes_and_excludes_siblings(self):
        with mock.patch.object(windows_package, "git_revision", return_value=("a" * 40, True)), mock.patch.object(windows_package, "authenticode", return_value={"status": "NotSigned", "thumbprint": None}):
            report = windows_package.package_preview(self.repository, self.executable, self.resources, self.repository / "windows" / "dist", git="unused")
        self.assertEqual(report["kind"], "developer-preview")
        self.assertIsNone(report["windows_version"])
        archive = self.repository / "windows" / "dist" / report["archive"]
        self.assertEqual(report["archive_sha256"], hashlib.sha256(archive.read_bytes()).hexdigest())
        with zipfile.ZipFile(archive) as package:
            names = package.namelist()
            self.assertIn("EndfieldHUD-Windows/PREVIEW.txt", names)
            self.assertFalse(any(name.endswith(".pdb") or "private-user-data" in name for name in names))
            self.assertEqual(sum(len(package.read(name)) for name in names), report["installed_bytes_including_inventory"])

    def test_mac_output_path_is_refused(self):
        with self.assertRaisesRegex(ValueError, "separate from Mac"):
            windows_package.package_preview(self.repository, self.executable, self.resources, self.repository / "dist", git="unused")

    def test_injected_documentation_file_is_refused(self):
        (self.resources / "recording.gif").write_bytes(b"synthetic forbidden documentation")
        with self.assertRaisesRegex(ValueError, "extra files"):
            stage_resources.verify_resources(self.resources)

    def test_altered_notice_is_refused(self):
        (self.resources / "LICENSE.txt").write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "Altered"):
            stage_resources.verify_resources(self.resources)

    def test_consumer_release_is_refused_even_with_signature(self):
        with mock.patch.object(windows_package, "git_revision", return_value=("a" * 40, False)), mock.patch.object(windows_package, "authenticode", return_value={"status": "Valid", "thumbprint": "TEST"}):
            with self.assertRaisesRegex(ValueError, "Consumer Windows release refused"):
                windows_package.package_preview(self.repository, self.executable, self.resources, self.repository / "windows" / "dist", git="unused", release=True, version="9.9.9")
        self.assertFalse((self.repository / "windows" / "dist").exists())

    def test_completed_synthetic_gates_dispatch_to_real_msix_path(self):
        artifact = self.repository / "synthetic-evidence.json"
        artifact.write_bytes(b'{"synthetic":true}')
        proof = {"path": artifact.name, "sha256": hashlib.sha256(artifact.read_bytes()).hexdigest()}
        evidence = {
            "schema": 1, "chosen_by": "DDDuoDuo", "windows_version": "9.9.9", "source_commit": "a" * 40,
            "executable_sha256": hashlib.sha256(self.executable.read_bytes()).hexdigest(), "signer_thumbprint": "TEST",
            "hardware": {"os_build": "synthetic", "cpu": "synthetic", "gpu": "synthetic", "ram_bytes": 1, "monitor_dpi": 96, "refresh_hz": 60},
            "gates": {name: {"status": "passed", "summary": "synthetic dispatch test", "artifacts": [proof]} for name in windows_package.RELEASE_GATES},
        }
        evidence_path = self.repository / "release-evidence.json"
        evidence_path.write_bytes(stage_resources.json_bytes(evidence))
        options = {"package_version": "9.9.9.0", "publisher": "CN=Synthetic", "logo_source": self.repository / "synthetic.png", "certificate_thumbprint": "TEST", "timestamp_uri": "https://example.test/time", "asset_base_uri": "https://example.test/windows/9.9.9", "feed_uri": "https://example.test/windows/feed.appinstaller"}
        with mock.patch.object(windows_package, "git_revision", return_value=("a" * 40, False)), mock.patch.object(windows_package, "authenticode", return_value={"status": "Valid", "thumbprint": "TEST"}), mock.patch.object(msix_release, "create_release", return_value={"synthetic_dispatch": True}) as create:
            report = windows_package.package_preview(self.repository, self.executable, self.resources, self.repository / "windows" / "dist", git="unused", release=True, version="9.9.9", evidence_path=evidence_path, release_options=options)
        self.assertTrue(report["synthetic_dispatch"])
        self.assertEqual(create.call_args.kwargs["version"], "9.9.9.0")
        self.assertFalse(create.call_args.kwargs["automatic_updates"])
        self.assertFalse((self.repository / "windows" / "dist").exists())


if __name__ == "__main__":
    unittest.main()
