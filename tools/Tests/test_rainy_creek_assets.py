"""Integrity and source-isolation regressions for the Rainy Creek selection."""

import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "PrepareRainyCreekAssets.py"
SPEC = importlib.util.spec_from_file_location("rainy_creek_assets", SCRIPT)
ASSETS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ASSETS)


class RainyCreekAssetTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="Lux Rainy Creek 한글 ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "Assets"
        shutil.copytree(ASSETS.DEFAULT_OUTPUT, self.root)

    def test_bundled_assets_verify_without_conversion_dependencies(self):
        result = subprocess.run(
            [sys.executable, "-S", str(SCRIPT), "--verify", "--output", str(self.root)],
            capture_output=True, text=True, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertEqual(report["models"], 7)
        self.assertEqual(report["meshes"], 14)
        self.assertLess(report["bytes"], ASSETS.MAX_OUTPUT_BYTES)
        self.assertFalse(report["canonicalPackageCreated"])

    def test_same_size_texture_corruption_is_rejected(self):
        path = self.root / "Textures/fern.png"
        data = bytearray(path.read_bytes())
        data[-1] ^= 1
        path.write_bytes(data)
        with self.assertRaisesRegex(ASSETS.AssetError, "integrity check failed"):
            ASSETS.verify(self.root)

    def test_private_code_or_unrecorded_file_is_rejected(self):
        (self.root / "CopiedEngine.cpp").write_text("unexpected code", encoding="utf-8")
        with self.assertRaisesRegex(ASSETS.AssetError, "unrecorded files"):
            ASSETS.verify(self.root)

    def test_source_code_is_not_an_accepted_input(self):
        source = Path(self.temporary.name) / "Sources"
        source.mkdir()
        path = source / "engine.cpp"
        path.write_text("unexpected code", encoding="utf-8")
        sources = {"files": [{"path": "engine.cpp", "bytes": path.stat().st_size,
                              "sha256": ASSETS.digest(path)}]}
        with self.assertRaisesRegex(ASSETS.AssetError, "Unapproved source file"):
            ASSETS.verify_sources(source, sources)

    def test_missing_license_and_forged_canonical_receipt_are_rejected(self):
        (self.root / "LICENSE.txt").unlink()
        with self.assertRaisesRegex(ASSETS.AssetError, "Required asset is missing"):
            ASSETS.verify(self.root)
        provenance_path = self.root / "Provenance.json"
        provenance = json.loads(provenance_path.read_text(encoding="utf-8"))
        provenance["admission"]["canonicalPackageCreated"] = True
        provenance_path.write_text(json.dumps(provenance), encoding="utf-8")
        with self.assertRaisesRegex(ASSETS.AssetError, "must not claim a canonical"):
            ASSETS.verify(self.root)

    def test_obj_rejects_nonfinite_coordinates_and_invalid_indices(self):
        path = Path(self.temporary.name) / "bad.obj"
        path.write_text("v nan 0 0\n", encoding="ascii")
        with self.assertRaisesRegex(ASSETS.AssetError, "Invalid OBJ attribute"):
            ASSETS.verify_obj(path)
        path.write_text(
            "v 0 0 0\nvt 0 0\nvn 0 1 0\nf 1/1/1 1/1/1 2/1/1\n",
            encoding="ascii",
        )
        with self.assertRaisesRegex(ASSETS.AssetError, "Out-of-range OBJ index"):
            ASSETS.verify_obj(path)

    def test_paths_cannot_escape_the_package(self):
        with self.assertRaisesRegex(ASSETS.AssetError, "Unsafe asset path"):
            ASSETS.owned_file(self.root, "../outside.obj")
        with self.assertRaisesRegex(ASSETS.AssetError, "Unsafe asset path"):
            ASSETS.owned_file(self.root, "/outside.obj")

    def test_existing_output_is_never_overwritten(self):
        # The source does not need to be converted to enforce an occupied
        # destination. Keep the sentinel independent of preparation packages.
        sentinel = self.root / "catalog.json"
        before = sentinel.read_bytes()
        command = [sys.executable, str(SCRIPT), "--source", str(self.root),
                   "--output", str(self.root)]
        result = subprocess.run(command, capture_output=True, text=True, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(sentinel.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
