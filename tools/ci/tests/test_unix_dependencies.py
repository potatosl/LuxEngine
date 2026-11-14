import os
from pathlib import Path
import subprocess
import tempfile
import unittest


SETUP_SCRIPT = Path(__file__).resolve().parents[1] / "SetupUnixDependencies.sh"


@unittest.skipIf(os.name == "nt", "Unix dependency preparation requires a POSIX shell")
class UnixDependenciesTests(unittest.TestCase):
    def run_cached_setup(self, platform, prefix_name, previous_path):
        with tempfile.TemporaryDirectory(prefix="lux-ci-dependencies-") as directory:
            root = Path(directory)
            prefix = root / prefix_name
            for relative in [
                "lib/cmake/SDL3/SDL3Config.cmake",
                "lib/cmake/Jolt/JoltConfig.cmake",
                "lib/cmake/Jolt/JoltConfig-debug.cmake",
                "lib/cmake/Jolt/JoltConfig-release.cmake",
                "include/stb/stb_image.h",
                "include/stb/stb_truetype.h",
            ]:
                path = prefix / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()

            commands = root / "commands.log"
            environment_file = root / "github-env"
            environment_file.touch()
            bin_directory = root / "bin"
            bin_directory.mkdir()
            stubs = {
                "uname": 'printf "%s\\n" "$LUX_CI_TEST_PLATFORM"',
                "sudo": 'printf "%s\\n" "$@" >> "$LUX_CI_COMMAND_LOG"',
                "brew": 'printf "%s\\n" "$@" >> "$LUX_CI_COMMAND_LOG"',
                "git": 'echo "Cached dependencies unexpectedly fetched sources" >&2; exit 97',
                "cmake": 'echo "Cached dependencies unexpectedly rebuilt sources" >&2; exit 97',
            }
            for name, body in stubs.items():
                stub = bin_directory / name
                stub.write_text("#!/bin/sh\n" + body + "\n", encoding="utf-8")
                stub.chmod(0o755)

            variable = "LD_LIBRARY_PATH" if platform == "Linux" else "DYLD_LIBRARY_PATH"
            environment = dict(os.environ)
            environment.update({
                "PATH": str(bin_directory) + os.pathsep + os.environ["PATH"],
                "GITHUB_ENV": str(environment_file),
                "LUX_CI_TEST_PLATFORM": platform,
                "LUX_CI_COMMAND_LOG": str(commands),
                "RUNNER_TEMP": str(root),
            })
            if previous_path is None:
                environment.pop(variable, None)
            else:
                environment[variable] = previous_path
            completed = subprocess.run(
                ["bash", str(SETUP_SCRIPT), str(prefix)],
                env=environment,
                capture_output=True,
                text=True,
            )
            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
            expected = f"{variable}={prefix}/lib"
            if previous_path:
                expected += ":" + previous_path
            self.assertEqual(environment_file.read_text(encoding="utf-8"), expected + "\n")
            return commands.read_text(encoding="utf-8").splitlines()

    def test_cached_prefix_runtime_paths(self):
        # Both loaders must preserve existing entries and paths containing spaces.
        for platform in ["Linux", "Darwin"]:
            for prefix_name in ["dependencies", "dependencies with spaces"]:
                for previous_path in [None, "/existing/path with spaces:/another/library"]:
                    with self.subTest(
                        platform=platform, prefix=prefix_name, existing=previous_path
                    ):
                        self.run_cached_setup(platform, prefix_name, previous_path)

    def test_linux_installs_all_libzip_imported_tools(self):
        commands = self.run_cached_setup("Linux", "dependencies", None)
        self.assertIn("apt-get", commands)
        self.assertIn("install", commands)
        self.assertIn("libzip-dev", commands)
        for package in ["zipcmp", "zipmerge", "ziptool"]:
            self.assertIn(package, commands)


if __name__ == "__main__":
    unittest.main()
