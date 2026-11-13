import importlib.util
import io
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock


MODULE_PATH = Path(__file__).resolve().parents[1] / "CheckFormatting.py"
SPEC = importlib.util.spec_from_file_location("check_formatting", MODULE_PATH)
formatting = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(formatting)


class FormattingTests(unittest.TestCase):
    def test_source_selection_excludes_dependencies_and_assets(self):
        for path in [
            "Engine/Scene/Public/LuxEngine/Scene.hpp",
            "Editor/App/Private/View.cpp",
            "samples/Game/App.inl",
            "tools/Viewer/Preview.h",
        ]:
            self.assertTrue(formatting.is_source(path), path)
        for path in [
            "Engine/ThirdParty/TheForge/Test.cpp",
            "samples/Game/Assets/source.hpp",
            "Editor/generated/moc_Test.cpp",
            "docs/example.cpp",
            "Engine/Scene/Scene.cmake",
        ]:
            self.assertFalse(formatting.is_source(path), path)

    def test_event_base_handles_pull_request_push_and_manual_runs(self):
        self.assertEqual(formatting.event_base({"pull_request": {"base": {"sha": "abc"}}}), "abc")
        self.assertEqual(formatting.event_base({"before": "def"}), "def")
        self.assertIsNone(formatting.event_base({"before": "0" * 40}))
        self.assertIsNone(formatting.event_base({}))

    def test_only_existing_regular_sources_are_returned_once(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "Editor" / "path with spaces.cpp"
            source.parent.mkdir()
            source.touch()
            paths = [
                b"Editor/path with spaces.cpp",
                b"Editor/path with spaces.cpp",
                b"Editor/deleted.cpp",
                b"",
            ]
            self.assertEqual(formatting.source_files(root, paths), ["Editor/path with spaces.cpp"])

    def test_format_errors_and_version_mismatch_fail_the_command(self):
        for version, result, should_run in [
            ("clang-format version 21.1.8", 1, True),
            ("clang-format version 19.1.7", 0, False),
        ]:
            with (
                self.subTest(version=version),
                mock.patch.object(formatting, "commit", return_value="valid"),
                mock.patch.object(formatting, "changed_files", return_value=[]),
                mock.patch.object(formatting, "source_files", return_value=["Editor/App.cpp"]),
                mock.patch.object(formatting.subprocess, "check_output", return_value=version),
                mock.patch.object(formatting.subprocess, "run") as run,
                redirect_stderr(io.StringIO()),
                redirect_stdout(io.StringIO()),
            ):
                run.return_value.returncode = result
                self.assertEqual(formatting.main(["--base", "base"]), 1)
                self.assertEqual(run.called, should_run)

    def test_git_diff_handles_spaces_rename_and_deletion(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q", str(root)], check=True)

            def run(*args):
                return formatting.git(root, *args)

            def save_commit():
                run("add", ".")
                run(
                    "-c", "user.email=test@example.invalid", "-c", "user.name=CI Test",
                    "commit", "-qm", "fixture",
                )
                return run("rev-parse", "HEAD").strip().decode()

            folder = root / "Editor"
            folder.mkdir()
            original = folder / "original.cpp"
            deleted = folder / "deleted.cpp"
            original.write_text("int same() { return 1; }\n", encoding="utf-8")
            deleted.write_text("int removed;\n", encoding="utf-8")
            base = save_commit()
            self.assertEqual(
                formatting.source_files(root, formatting.changed_files(root, None, base)),
                ["Editor/deleted.cpp", "Editor/original.cpp"],
            )
            original.rename(folder / "renamed with spaces.cpp")
            deleted.unlink()
            (folder / "new.inl").write_text("int added;\n", encoding="utf-8")
            head = save_commit()
            expected = ["Editor/new.inl", "Editor/renamed with spaces.cpp"]
            self.assertEqual(formatting.source_files(root, formatting.changed_files(root, base, head)), expected)
            self.assertEqual(formatting.source_files(root, formatting.changed_files(root, None, head)), expected)
            with self.assertRaises(subprocess.CalledProcessError):
                formatting.commit(root, "--output=/tmp/unexpected")


if __name__ == "__main__":
    unittest.main()
