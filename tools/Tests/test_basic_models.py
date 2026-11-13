"""Regressions for the manually reviewed CC0 FBX package's byte integrity."""

import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "VerifyBasicModels.py"
SPEC = importlib.util.spec_from_file_location("verify_basic_models", SCRIPT)
VERIFY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERIFY)


class BasicModelIntegrityTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="Lux basic models 한글 ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "KenneyPrototype"
        shutil.copytree(VERIFY.DEFAULT_ROOT, self.root)

    def test_bundled_selection_and_license_pass(self):
        result = VERIFY.verify_pack(self.root)
        self.assertEqual(result["models"], 10)
        self.assertEqual(result["source_files"], 12)
        self.assertEqual(result["license"], "CC0-1.0")
        self.assertFalse(result["canonical_package_created"])

    def test_same_size_model_replacement_is_rejected(self):
        path = self.root / "shape-cube.fbx"
        original = path.read_bytes()
        path.write_bytes(bytes([original[0] ^ 1]) + original[1:])
        with self.assertRaisesRegex(VERIFY.VerificationError, "differs from its recorded source"):
            VERIFY.verify_pack(self.root)

    def test_missing_license_is_rejected(self):
        (self.root / "License.txt").unlink()
        with self.assertRaisesRegex(VERIFY.VerificationError, "Required package file is missing"):
            VERIFY.verify_pack(self.root)

    def test_unlisted_file_and_missing_roster_entry_are_rejected(self):
        extra = self.root / "extra.fbx"
        extra.write_bytes(b"unreviewed")
        with self.assertRaisesRegex(VERIFY.VerificationError, "outside the approved roster"):
            VERIFY.verify_pack(self.root)
        extra.unlink()
        manifest_path = self.root / "Provenance.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["files"].pop()
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        with self.assertRaisesRegex(VERIFY.VerificationError, "exactly 12"):
            VERIFY.verify_pack(self.root)

    def test_failure_returns_nonzero_to_callers(self):
        (self.root / "Textures/colormap.png").unlink()
        result = subprocess.run(
            [sys.executable, str(SCRIPT), "--root", str(self.root), "--json"],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(result.returncode, 1)
        self.assertFalse(json.loads(result.stdout)["ok"])


if __name__ == "__main__":
    unittest.main()
