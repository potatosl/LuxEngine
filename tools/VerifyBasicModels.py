#!/usr/bin/env python3
"""Verify the bundled Kenney FBX selection against its recorded source roster."""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import sys
from zipfile import BadZipFile, ZipFile


DEFAULT_ROOT = (
    Path(__file__).resolve().parents[1]
    / "Engine/Resources/Models/Primitives/KenneyPrototype"
)
ARCHIVE_BYTES = 2961396
ARCHIVE_SHA256 = "213b522fb12bcc9b9ac66c4f7581f7c74623293272212e40a70c39936ad3da95"
MODEL_IDS = (
    "shape-cube",
    "shape-cylinder",
    "floor-square",
    "shape-cube-rounded",
    "shape-slope",
    "shape-triangular-prism",
    "shape-hexagon",
    "shape-cube-half",
    "shape-cylinder-detailed",
    "shape-cylinder-half",
)
EXPECTED_PATHS = {f"{model_id}.fbx" for model_id in MODEL_IDS} | {
    "Textures/colormap.png",
    "License.txt",
}


class VerificationError(ValueError):
    pass


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def owned_file(root, relative):
    path = PurePosixPath(relative)
    if path.is_absolute() or ".." in path.parts or "\\" in relative:
        raise VerificationError(f"Invalid package path: {relative}")
    candidate = root
    for part in path.parts:
        candidate /= part
        if candidate.is_symlink():
            raise VerificationError(f"Package symlink is not allowed: {relative}")
    if not candidate.is_file():
        raise VerificationError(f"Required package file is missing: {relative}")
    return candidate


def verify_pack(root, source_archive=None):
    root = Path(root).absolute()
    if root.is_symlink() or not root.is_dir():
        raise VerificationError(f"Package root must be an owned directory: {root}")
    manifest_path = owned_file(root, "Provenance.json")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if not isinstance(manifest, dict):
        raise VerificationError("Basic model provenance must be a JSON object")
    if manifest.get("schema") != "luxengine.basic_model_provenance.v1":
        raise VerificationError("Unsupported basic model provenance schema")
    if manifest.get("license") != "CC0-1.0" or manifest.get("author") != "Kenney":
        raise VerificationError("The recorded Kenney CC0 license identity is invalid")
    source = manifest.get("source", {})
    if (
        not isinstance(source, dict)
        or source.get("archive_bytes") != ARCHIVE_BYTES
        or source.get("archive_sha256") != ARCHIVE_SHA256
    ):
        raise VerificationError("Source archive identity differs from the reviewed download")
    admission = manifest.get("admission", {})
    if (
        not isinstance(admission, dict)
        or admission.get("method") != "direct_verified_copy"
        or admission.get("canonical_package_created") is not False
    ):
        raise VerificationError("The direct-copy exception must not claim a canonical receipt")

    files = manifest.get("files", [])
    if not isinstance(files, list) or len(files) != len(EXPECTED_PATHS):
        raise VerificationError("The source roster must contain exactly 12 selected files")
    listed = set()
    models = set()
    for entry in files:
        if not isinstance(entry, dict):
            raise VerificationError("Invalid source roster entry")
        relative = entry.get("path")
        if relative not in EXPECTED_PATHS or relative in listed:
            raise VerificationError(f"Unexpected or duplicate source roster path: {relative}")
        listed.add(relative)
        path = owned_file(root, relative)
        if path.stat().st_size != entry.get("bytes") or sha256(path) != entry.get("sha256"):
            raise VerificationError(f"Copied file differs from its recorded source: {relative}")
        if relative.endswith(".fbx"):
            model_id = Path(relative).stem
            if (
                entry.get("id") != model_id
                or entry.get("role") != "model"
                or entry.get("source_path") != f"Models/FBX format/{relative}"
            ):
                raise VerificationError(f"Invalid model source identity: {relative}")
            models.add(model_id)
        else:
            role, original = (
                ("license", "License.txt")
                if relative == "License.txt"
                else ("texture", "Models/FBX format/Textures/colormap.png")
            )
            if entry.get("role") != role or entry.get("source_path") != original:
                raise VerificationError(f"Invalid support file identity: {relative}")
    if listed != EXPECTED_PATHS or models != set(MODEL_IDS):
        raise VerificationError("The model selection differs from the approved roster")

    actual_files = set()
    for path in root.rglob("*"):
        if path.is_symlink():
            raise VerificationError(f"Package symlink is not allowed: {path.relative_to(root)}")
        if path.is_file():
            actual_files.add(path.relative_to(root).as_posix())
    if actual_files != EXPECTED_PATHS | {"Provenance.json"}:
        raise VerificationError("Package contains files outside the approved roster")

    if source_archive is not None:
        source_archive = Path(source_archive)
        if (
            source_archive.stat().st_size != ARCHIVE_BYTES
            or sha256(source_archive) != ARCHIVE_SHA256
        ):
            raise VerificationError("Source ZIP differs from the reviewed download")
        with ZipFile(source_archive) as archive:
            if archive.testzip() is not None:
                raise VerificationError("Source ZIP CRC verification failed")
            for entry in files:
                original = entry["source_path"]
                if archive.namelist().count(original) != 1:
                    raise VerificationError(f"Source ZIP entry is missing or duplicated: {original}")
                data = archive.read(original)
                if (
                    len(data) != entry["bytes"]
                    or hashlib.sha256(data).hexdigest() != entry["sha256"]
                ):
                    raise VerificationError(f"Source ZIP entry differs from the roster: {original}")

    return {
        "ok": True,
        "root": str(root),
        "models": len(models),
        "source_files": len(files),
        "license": manifest["license"],
        "manifest_sha256": sha256(manifest_path),
        "source_archive_verified": source_archive is not None,
        "canonical_package_created": False,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=DEFAULT_ROOT)
    parser.add_argument("--source-archive", type=Path, help="Also re-verify the original source ZIP")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    try:
        result = verify_pack(args.root, args.source_archive)
    except (OSError, ValueError, BadZipFile, KeyError, TypeError) as error:
        if args.json:
            print(json.dumps({"ok": False, "error": str(error)}, ensure_ascii=False))
        else:
            print(f"기본 모델 검증에 실패했습니다: {error}", file=sys.stderr)
        return 1
    if args.json:
        print(json.dumps(result, ensure_ascii=False, indent=2))
    else:
        print("기본 모델 10개와 라이선스·텍스처 검증을 완료했습니다.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
