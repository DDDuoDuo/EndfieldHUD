#!/usr/bin/env python3
"""Permission regression fixtures; never touch an installed application."""
import importlib.util
import os
from pathlib import Path
import shutil
import stat
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("bundle_permissions", Path(__file__).with_name("normalize-bundle-permissions.py"))
permissions = importlib.util.module_from_spec(spec)
spec.loader.exec_module(permissions)


class BundlePermissionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="EndfieldHUD-mode-fixture-")
        self.root = Path(self.temporary.name)
        self.app = self.root / "Fixture.app"
        (self.app / "Contents/Resources").mkdir(parents=True)
        self.resource = self.app / "Contents/Resources/icon.png"
        self.resource.write_bytes(b"fixture resource")
        self.resource.chmod(0o600)

    def tearDown(self):
        self.temporary.cleanup()

    def test_owner_only_resource_is_rejected_without_mutation(self):
        with self.assertRaisesRegex(ValueError, "icon.png is 0600; expected 0644"):
            permissions.process_bundle(self.app, check=True)
        self.assertEqual(stat.S_IMODE(self.resource.stat().st_mode), 0o600)

    def test_owner_only_root_and_directory_are_rejected(self):
        self.resource.chmod(0o644)
        for path in (self.app, self.app / "Contents/Resources"):
            path.chmod(0o700)
            with self.assertRaisesRegex(ValueError, "is 0700; expected 0755"):
                permissions.process_bundle(self.app, check=True)
            path.chmod(0o755)

    def test_staging_normalization_preserves_source_bytes_and_links(self):
        self.app.chmod(0o700)
        (self.app / "Contents/Resources").chmod(0o700)
        executable = self.app / "Contents/helper"
        executable.write_bytes(b"fixture executable")
        executable.chmod(0o700)
        (self.app / "Contents/current-icon").symlink_to("Resources/icon.png")
        staging = self.root / "Staged.app"
        shutil.copytree(self.app, staging, symlinks=True)
        _, changed = permissions.process_bundle(staging)
        self.assertEqual(changed, 4)
        permissions.process_bundle(staging, check=True)
        self.assertEqual((staging / "Contents/Resources/icon.png").read_bytes(), self.resource.read_bytes())
        self.assertEqual(os.readlink(staging / "Contents/current-icon"), "Resources/icon.png")
        self.assertEqual(stat.S_IMODE((staging / "Contents/helper").stat().st_mode), 0o755)
        self.assertEqual(stat.S_IMODE(self.app.stat().st_mode), 0o700)
        self.assertEqual(stat.S_IMODE(self.resource.stat().st_mode), 0o600)
        self.assertEqual(stat.S_IMODE(executable.stat().st_mode), 0o700)

    def test_external_symlink_is_rejected_without_touching_target(self):
        outside = self.root / "outside"
        outside.write_text("outside")
        outside.chmod(0o600)
        (self.app / "Contents/escape").symlink_to(outside)
        with self.assertRaisesRegex(ValueError, "Symlink escapes bundle"):
            permissions.process_bundle(self.app)
        self.assertEqual(stat.S_IMODE(outside.stat().st_mode), 0o600)
        self.assertEqual(stat.S_IMODE(self.resource.stat().st_mode), 0o600)

    def test_external_hardlink_is_rejected(self):
        outside = self.root / "outside"
        outside.write_text("outside")
        outside.chmod(0o600)
        os.link(outside, self.app / "Contents/alias")
        with self.assertRaisesRegex(ValueError, "Hard-linked file"):
            permissions.process_bundle(self.app)
        self.assertEqual(stat.S_IMODE(outside.stat().st_mode), 0o600)


if __name__ == "__main__":
    unittest.main()
