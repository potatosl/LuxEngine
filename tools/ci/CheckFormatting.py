#!/usr/bin/env python3
"""Check changed first-party C/C++ files without rewriting the working tree."""

import argparse
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys


FORMAT_VERSION = "21.1.8"
SOURCE_ROOTS = {"Engine", "Editor", "samples", "tools"}
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl"}
EXCLUDED_PARTS = {"assets", "thirdparty", "third_party", "vendor", "generated"}


def git(root, *arguments):
    return subprocess.check_output(["git", *arguments], cwd=root)


def is_source(path):
    candidate = PurePosixPath(path)
    return (
        bool(candidate.parts)
        and candidate.parts[0] in SOURCE_ROOTS
        and candidate.suffix.lower() in SOURCE_SUFFIXES
        and not any(part.lower() in EXCLUDED_PARTS for part in candidate.parts[:-1])
    )


def commit(root, revision):
    # Never interpret event data as a command option or shell source.
    return git(root, "rev-parse", "--verify", "--end-of-options", revision + "^{commit}").strip().decode()


def event_base(event):
    if "pull_request" in event:
        return event["pull_request"]["base"]["sha"]
    before = event.get("before", "")
    if before and set(before) != {"0"}:
        return before
    return None


def changed_files(root, base, head):
    if base is None:
        # A manual run checks the latest commit. An initial commit checks its complete tree.
        parents = git(root, "rev-list", "--parents", "-n", "1", head).split()
        if len(parents) > 1:
            base = parents[1].decode()
        else:
            return git(root, "ls-tree", "-r", "--name-only", "-z", head).split(b"\0")
    return git(root, "diff", "--name-only", "-z", "--diff-filter=ACMR", base, head, "--").split(b"\0")


def source_files(root, paths):
    result = []
    for raw in paths:
        if not raw:
            continue
        relative = os.fsdecode(raw)
        path = root / relative
        # Do not follow source symlinks into an external dependency or outside the checkout.
        if is_source(relative) and path.is_file() and not path.is_symlink():
            result.append(relative)
    return sorted(set(result))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", help="Commit before the change; defaults to the GitHub event base")
    parser.add_argument("--head", default="HEAD", help="Checked-out commit to compare")
    parser.add_argument("--event-path", default=os.environ.get("GITHUB_EVENT_PATH"))
    parser.add_argument("--clang-format", default="clang-format")
    parser.add_argument("--all", action="store_true", help="Audit every tracked first-party source")
    args = parser.parse_args(argv)
    root = Path(__file__).resolve().parents[2]
    try:
        head = commit(root, args.head)
        base = args.base
        if base is None and args.event_path:
            base = event_base(json.loads(Path(args.event_path).read_text(encoding="utf-8")))
        if base:
            base = commit(root, base)
        paths = git(root, "ls-files", "-z").split(b"\0") if args.all else changed_files(root, base, head)
        files = source_files(root, paths)
        if not files:
            print("No changed first-party C/C++ files to format-check.")
            return 0
        version = subprocess.check_output([args.clang_format, "--version"], text=True)
        if not re.search(r"\bversion " + re.escape(FORMAT_VERSION) + r"\b", version):
            raise ValueError(f"clang-format {FORMAT_VERSION} is required; found {version.strip()}")
        failed = []
        for path in files:
            result = subprocess.run(
                [args.clang_format, "--dry-run", "--Werror", "--", path], cwd=root, check=False
            )
            if result.returncode:
                failed.append(path)
        print(f"Checked {len(files)} source files with clang-format {FORMAT_VERSION}.")
        if failed:
            print("Formatting failed: " + ", ".join(failed), file=sys.stderr)
            return 1
        return 0
    except (OSError, subprocess.CalledProcessError, ValueError, KeyError) as error:
        print(f"Formatting check failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
