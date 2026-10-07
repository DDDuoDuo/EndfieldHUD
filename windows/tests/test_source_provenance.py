"""Git authority and exact-byte staging checks using only synthetic folders."""
from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "packaging"))
import source_provenance as proof
import stage_resources


@unittest.skipUnless(shutil.which("git"), "Git is required for synthetic fixtures")
class SourceProvenanceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "fresh-repository"
        self.root.mkdir()
        self.git("init", "--initial-branch=" + proof.REF)
        self.git("config", "core.autocrlf", "false")
        self.git("config", "user.name", "Synthetic Fixture")
        self.git("config", "user.email", "fixture@example.invalid")
        self.git("remote", "add", "origin", proof.ORIGIN)
        self.write("Resources/asset.png", b"synthetic original image")
        self.write("Resources/other.png", b"other original image")
        self.write("Resources/metadata.json", b'{"id":"-9088776655443322110","value":0.123456789012345678901}\n')
        self.write(".gitattributes", b"Resources/metadata.json text\n")
        self.write("windows/resources/runtime-assets.json", b"{}\n")
        self.git("add", ".")
        self.git("commit", "-m", "Synthetic canonical inputs")
        self.baseline = self.git("rev-parse", "HEAD")
        self.git("update-ref", "refs/remotes/origin/" + proof.REF, self.baseline)
        self.resources = self.root / "windows/build/synthetic/Resources"

    def git(self, *arguments):
        return subprocess.check_output(["git", "-C", str(self.root), *arguments], stderr=subprocess.PIPE, text=True).strip()

    def write(self, name, data):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def authority(self):
        return proof.SourceAuthority(self.root, expected_repository=self.root, baseline=self.baseline)

    def fixture(self):
        authority = self.authority()
        self.resources.mkdir(parents=True)
        outputs = []
        for name in ("asset.png", "metadata.json"):
            data = authority.read("Resources/" + name)
            target = self.resources / name
            target.write_bytes(data)
            outputs.append(proof.output_record(target, self.resources, inputs=["Resources/" + name], decoded=data))
        manifest = proof.create_manifest(authority, outputs, self.root / "windows/resources/runtime-assets.json")
        self.save(manifest)
        (self.resources / "resources-inventory.json").write_bytes(b"{}\n")
        return manifest

    def save(self, manifest):
        (self.resources / proof.PROVENANCE).write_bytes(stage_resources.json_bytes(manifest))

    def test_original_bytes_and_signed_tokens_are_proven(self):
        manifest = self.fixture()
        self.assertEqual(proof.verify_manifest(self.resources, authority=self.authority()), manifest)
        self.assertEqual(manifest["source"]["baseline_commit"], self.baseline)
        self.assertEqual(manifest["acceptance"], "restart-build-only-unverified")
        self.assertFalse(manifest["source"]["local_fallback"])

    def test_repository_argument_cannot_select_old_checkout(self):
        other = Path(self.temporary.name) / "old-repository"
        other.mkdir()
        with self.assertRaisesRegex(ValueError, "fresh repository"):
            proof.SourceAuthority(other, expected_repository=self.root, baseline=self.baseline)
        with self.assertRaisesRegex(ValueError, "fresh repository"):
            stage_resources.stage_resources(other, other / "windows/build/Resources")

    def test_wrong_origin_is_rejected(self):
        self.git("remote", "set-url", "origin", "https://github.com/example/wrong.git")
        with self.assertRaisesRegex(ValueError, "origin"):
            self.authority()

    def test_multiple_origin_urls_are_ambiguous(self):
        self.git("remote", "set-url", "--add", "origin", "https://github.com/example/other.git")
        with self.assertRaisesRegex(ValueError, "ambiguous"):
            self.authority()

    def test_wrong_branch_and_detached_head_are_rejected(self):
        self.git("checkout", "-b", "wrong-branch")
        with self.assertRaisesRegex(ValueError, "branch"):
            self.authority()
        self.git("checkout", "--detach", self.baseline)
        with self.assertRaises(ValueError):
            self.authority()

    def test_modified_tracked_source_is_rejected(self):
        self.write("Resources/asset.png", b"locally modified image")
        with self.assertRaisesRegex(ValueError, "canonical Git blob"):
            self.authority().read("Resources/asset.png")

    def test_newline_conversion_is_rejected_even_when_git_clean(self):
        self.git("config", "core.autocrlf", "true")
        original = (self.root / "Resources/metadata.json").read_bytes()
        self.write("Resources/metadata.json", original.replace(b"\n", b"\r\n"))
        # Refresh Git's stat cache using its normal clean filter; the canonical
        # blob remains unchanged while checkout bytes are now CRLF.
        self.git("add", "Resources/metadata.json")
        self.assertEqual(self.git("status", "--porcelain", "--", "Resources/metadata.json"), "")
        with self.assertRaisesRegex(ValueError, "newline conversion"):
            self.authority().read("Resources/metadata.json")

    def test_committed_asset_change_cannot_replace_baseline(self):
        self.write("Resources/asset.png", b"committed replacement")
        self.git("add", "Resources/asset.png")
        self.git("commit", "-m", "Synthetic changed asset")
        with self.assertRaisesRegex(ValueError, "Committed source input"):
            self.authority().read("Resources/asset.png")

    def test_untracked_source_cannot_enter_selection(self):
        self.write("Resources/local-only.png", b"untracked")
        with self.assertRaisesRegex(ValueError, "untracked inputs"):
            self.authority().audit_tree("Resources")

    def test_missing_source_tree_file_is_rejected(self):
        (self.root / "Resources/other.png").unlink()
        with self.assertRaisesRegex(ValueError, "missing or untracked"):
            self.authority().audit_tree("Resources")

    def test_missing_provenance_is_rejected(self):
        self.resources.mkdir(parents=True)
        with self.assertRaisesRegex(ValueError, "Missing provenance input"):
            proof.verify_manifest(self.resources, authority=self.authority())

    def test_outside_staging_root_is_rejected(self):
        outside = Path(self.temporary.name) / "old-staged-assets"
        outside.mkdir()
        with self.assertRaisesRegex(ValueError, "fresh repository"):
            proof.verify_manifest(outside, authority=self.authority())

    def test_changed_staged_bytes_cannot_be_approved_by_new_hashes(self):
        manifest = self.fixture()
        target = self.resources / "asset.png"
        target.write_bytes(b"wrong artwork")
        manifest["outputs"][0] = proof.output_record(target, self.resources, inputs=["Resources/asset.png"], decoded=target.read_bytes())
        self.save(manifest)
        with self.assertRaisesRegex(ValueError, "byte-exact canonical source"):
            proof.verify_manifest(self.resources, authority=self.authority())

    def test_other_canonical_image_cannot_replace_named_asset(self):
        manifest = self.fixture()
        authority = self.authority()
        target = self.resources / "asset.png"
        target.write_bytes(authority.read("Resources/other.png"))
        manifest["inputs"].append(authority.used["Resources/other.png"])
        manifest["outputs"][0] = proof.output_record(target, self.resources, inputs=["Resources/other.png"], decoded=target.read_bytes())
        self.save(manifest)
        with self.assertRaisesRegex(ValueError, "byte-exact canonical source"):
            proof.verify_manifest(self.resources, authority=self.authority())

    def test_unknown_derivation_cannot_skip_source_equality(self):
        manifest = self.fixture()
        manifest["outputs"][0]["algorithm"] = "trust-the-local-copy"
        self.save(manifest)
        with self.assertRaisesRegex(ValueError, "Unknown provenance derivation"):
            proof.verify_manifest(self.resources, authority=self.authority())

    def test_unknown_encoding_is_rejected(self):
        manifest = self.fixture()
        manifest["outputs"][0]["encoding"] = "unchecked-copy"
        self.save(manifest)
        with self.assertRaisesRegex(ValueError, "Unknown provenance encoding"):
            proof.verify_manifest(self.resources, authority=self.authority())

    def test_source_change_after_staging_is_rejected(self):
        self.fixture()
        self.write("Resources/asset.png", b"source changed later")
        with self.assertRaisesRegex(ValueError, "canonical Git blob"):
            proof.verify_manifest(self.resources, authority=self.authority())

    def test_extra_staged_file_is_rejected(self):
        self.fixture()
        (self.resources / "local-only.png").write_bytes(b"unapproved")
        with self.assertRaisesRegex(ValueError, "extra staged"):
            proof.verify_manifest(self.resources, authority=self.authority())

    def test_windows_code_commit_requires_fresh_staging_metadata(self):
        self.fixture()
        self.write("windows/synthetic-code.txt", b"new code")
        self.git("add", "windows/synthetic-code.txt")
        self.git("commit", "-m", "Synthetic Windows code change")
        authority = self.authority()
        self.assertEqual(authority.read("Resources/asset.png"), b"synthetic original image")
        with self.assertRaisesRegex(ValueError, "stale"):
            proof.verify_manifest(self.resources, authority=authority)


if __name__ == "__main__":
    unittest.main()
