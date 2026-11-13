#!/usr/bin/env python3
"""Convert verified local Poly Haven data for Rainy Creek, or verify the result.

Preparation requires NumPy, Pillow, meshoptimizer and OpenCV. Normal builds and
--verify require only Python's standard library. Nothing is downloaded or run
from the source directory; proprietary engine code is never an input.
"""

import argparse
import hashlib
import importlib.metadata
import json
import math
from pathlib import Path, PurePosixPath
import shutil
import struct
import sys
import tempfile


REPOSITORY = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = REPOSITORY / "samples/RainyCreek/Assets"
SOURCE_ROSTER = Path(__file__).with_name("RainyCreekSources.json")
SOURCE_EXTENSIONS = {".gltf", ".bin", ".jpg", ".hdr"}
OUTPUT_EXTENSIONS = {".obj", ".png", ".hdr", ".json", ".txt"}
MAX_OUTPUT_BYTES = 35 * 1024 * 1024
MODEL_SELECTION = (
    ("rock_a", "rock/rock.gltf", 0, 9000, None, None),
    ("rock_b", "rock/rock.gltf", 2, 5000, None, None),
    ("rock_c", "rock/rock.gltf", 3, 9000, None, None),
    ("fern_a", "fern_02/fern_02.gltf", 0, 2500, None, None),
    ("fern_b", "fern_02/fern_02.gltf", 2, 1000, None, None),
    ("trunk", "dead_tree_trunk/dead_tree_trunk.gltf", 0, 25000, 50000, 8000),
    ("pine", "pine_sapling_small/pine_sapling_small.gltf", 0, 28000, 50000, 12000),
)
MATERIALS = {
    "rock_moss_set_01": ("rock", 0.88, False),
    "fern_02": ("fern", 0.86, True),
    "dead_tree_trunk": ("trunk", 0.92, False),
    "pine_sapling_small_bark": ("pine_bark", 0.94, False),
    "pine_sapling_small_twig": ("pine_needles", 0.90, True),
}


class AssetError(ValueError):
    pass


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def owned_file(root, relative):
    path = PurePosixPath(relative)
    if path.is_absolute() or ".." in path.parts or "\\" in relative:
        raise AssetError(f"Unsafe asset path: {relative}")
    candidate = root
    for part in path.parts:
        candidate /= part
        if candidate.is_symlink():
            raise AssetError(f"Asset symlinks are not allowed: {relative}")
    if not candidate.is_file():
        raise AssetError(f"Required asset is missing: {relative}")
    return candidate


def roster():
    result = json.loads(SOURCE_ROSTER.read_text(encoding="utf-8"))
    if result.get("schema") != "luxengine.rainy_creek.sources.v1":
        raise AssetError("Unsupported source roster")
    if result.get("license") != "CC0-1.0":
        raise AssetError("Only the reviewed CC0 sources are accepted")
    return result


def verify_sources(root, sources):
    if root.is_symlink() or not root.is_dir():
        raise AssetError("The source directory must exist and cannot be a symlink")
    expected = set()
    for entry in sources["files"]:
        relative = entry["path"]
        path = owned_file(root, relative)
        if path.suffix.lower() not in SOURCE_EXTENSIONS or relative in expected:
            raise AssetError(f"Unapproved source file: {relative}")
        if path.stat().st_size != entry["bytes"] or digest(path) != entry["sha256"]:
            raise AssetError(f"Source integrity check failed: {relative}")
        expected.add(relative)
    actual = set()
    for path in root.rglob("*"):
        if path.is_symlink():
            raise AssetError(f"Source symlinks are not allowed: {path.name}")
        if path.is_file():
            actual.add(path.relative_to(root).as_posix())
    if actual != expected:
        raise AssetError("Source directory contains files outside the reviewed data roster")


def read_gltf(path):
    import numpy as np

    document = json.loads(path.read_text(encoding="utf-8"))
    if document["asset"]["version"] != "2.0" or document.get("extensionsRequired"):
        raise AssetError("Only the reviewed, uncompressed glTF 2.0 inputs are supported")
    buffers = []
    for entry in document["buffers"]:
        buffers.append(owned_file(path.parent, entry["uri"]).read_bytes())

    def accessor(index):
        entry = document["accessors"][index]
        if "sparse" in entry or entry.get("normalized", False):
            raise AssetError("Sparse or normalized accessors are not in the reviewed inputs")
        view = document["bufferViews"][entry["bufferView"]]
        types = {5121: "<u1", 5123: "<u2", 5125: "<u4", 5126: "<f4"}
        dimensions = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}
        dtype = np.dtype(types[entry["componentType"]])
        columns = dimensions[entry["type"]]
        offset = view.get("byteOffset", 0) + entry.get("byteOffset", 0)
        stride = view.get("byteStride", columns * dtype.itemsize)
        return np.ndarray(
            shape=(entry["count"], columns),
            dtype=dtype,
            buffer=buffers[view["buffer"]],
            offset=offset,
            strides=(stride, dtype.itemsize),
        ).copy()

    return document, accessor


def simplify(positions, normals, uvs, indices, triangles, foliage):
    import ctypes
    import meshoptimizer
    import numpy as np

    if indices.size <= triangles * 3:
        return indices, 0.0
    destination = np.empty_like(indices)
    attributes = np.ascontiguousarray(np.concatenate((normals, uvs), axis=1), dtype=np.float32)
    weights = np.array((0.15, 0.15, 0.15, 0.5, 0.5), dtype=np.float32)
    error = np.zeros(1, dtype=np.float32)
    # Attribute-aware collapse keeps source UV seams and normals. Pruning small
    # disconnected leaf clusters is allowed only inside the same error bound.
    options = meshoptimizer.SIMPLIFY_PRUNE if foliage else 0
    # meshoptimizer-python 0.2.30a0 omits the ctypes declaration for this
    # exported API. Bind its public C signature explicitly, without changing
    # the installed package or depending on an untyped call.
    api = meshoptimizer.simplifier.lib.meshopt_simplifyWithAttributes
    unsigned_pointer = ctypes.POINTER(ctypes.c_uint)
    float_pointer = ctypes.POINTER(ctypes.c_float)
    api.argtypes = [
        unsigned_pointer, unsigned_pointer, ctypes.c_size_t,
        float_pointer, ctypes.c_size_t, ctypes.c_size_t,
        float_pointer, ctypes.c_size_t, float_pointer, ctypes.c_size_t,
        ctypes.POINTER(ctypes.c_ubyte), ctypes.c_size_t, ctypes.c_float,
        ctypes.c_uint, float_pointer,
    ]
    api.restype = ctypes.c_size_t
    count = meshoptimizer.simplify_with_attributes(
        destination,
        indices,
        positions,
        attributes,
        weights,
        target_index_count=triangles * 3,
        target_error=0.025 if foliage else 0.012,
        options=options,
        result_error=error,
    )
    if count == 0 or count % 3 != 0 or count > indices.size:
        raise AssetError("Mesh simplification produced an invalid triangle count")
    return destination[:count], float(error[0])


def write_obj(path, positions, normals, uvs, indices):
    import numpy as np

    used, remapped = np.unique(indices, return_inverse=True)
    positions = positions[used]
    normals = normals[used]
    uvs = uvs[used]
    if not all(np.isfinite(array).all() for array in (positions, normals, uvs)):
        raise AssetError("Non-finite vertex data is not allowed")
    path.parent.mkdir(parents=True, exist_ok=True)
    unique_positions, position_map = np.unique(positions, axis=0, return_inverse=True)
    unique_normals, normal_map = np.unique(normals, axis=0, return_inverse=True)
    unique_uvs, uv_map = np.unique(uvs, axis=0, return_inverse=True)
    with path.open("w", encoding="ascii", newline="\n") as stream:
        stream.write("# Poly Haven CC0; independently converted for LuxEngine. Y-up, meters.\n")
        stream.write("# Texture V is top-left, matching LuxEngine's OBJ loader.\n")
        for x, y, z in unique_positions:
            stream.write(f"v {x:.6g} {y:.6g} {z:.6g}\n")
        for u, v in unique_uvs:
            stream.write(f"vt {u:.6g} {v:.6g}\n")
        for x, y, z in unique_normals:
            stream.write(f"vn {x:.6g} {y:.6g} {z:.6g}\n")
        for triangle in remapped.reshape((-1, 3)):
            corners = [
                f"{position_map[i] + 1}/{uv_map[i] + 1}/{normal_map[i] + 1}"
                for i in triangle
            ]
            stream.write("f " + " ".join(corners) + "\n")
    return {
        "vertices": len(positions),
        "triangles": int(len(indices) // 3),
        "boundsMin": positions.min(axis=0).tolist(),
        "boundsMax": positions.max(axis=0).tolist(),
    }


def prepare_texture(source, output, relative, alpha=None):
    from PIL import Image

    image = Image.open(source).convert("RGBA")
    image = image.resize((1024, 1024), Image.Resampling.LANCZOS)
    if alpha is not None:
        mask = Image.open(alpha).convert("L")
        mask = mask.resize((1024, 1024), Image.Resampling.LANCZOS)
        # Current Lux texture shaders discard zero alpha. Encode the glTF 0.5
        # cutoff explicitly instead of relying on an unsupported blend mode.
        image.putalpha(mask.point(lambda value: 255 if value >= 128 else 0))
    path = output / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path, optimize=True)


def prepare_environment(source, output):
    import cv2
    import numpy as np

    image = cv2.imread(str(source), cv2.IMREAD_UNCHANGED)
    if image is None or image.ndim != 3 or image.shape[2] != 3:
        raise AssetError("Expected a Radiance RGB HDR image")
    image = cv2.resize(image, (1024, 512), interpolation=cv2.INTER_AREA)
    if not np.isfinite(image).all() or image.min() < 0 or image.max() <= 0:
        raise AssetError("Invalid HDR radiance values")
    output.parent.mkdir(parents=True, exist_ok=True)
    if not cv2.imwrite(str(output), image):
        raise AssetError("HDR encoding failed")


def prepare(source, output):
    if output.exists():
        raise AssetError(
            "Preparation requires a new output directory; existing assets are never overwritten"
        )
    sources = roster()
    verify_sources(source, sources)
    import numpy as np

    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="rainy-creek-", dir=output.parent) as temporary:
        staging = Path(temporary)
        texture_inputs = {
            "ground": ("forest_floor_diff.jpg", None),
            "rock": ("rock/textures/rock_moss_set_01_diff_2k.jpg", None),
            "fern": ("fern_02/textures/fern_02_diff_2k.jpg", "fern_02/alpha.jpg"),
            "trunk": ("dead_tree_trunk/textures/dead_tree_trunk_diff_2k.jpg", None),
            "pine_bark": ("pine_sapling_small/textures/pine_sapling_small_bark_diff_2k.jpg", None),
            "pine_needles": (
                "pine_sapling_small/textures/pine_sapling_small_twig_diff_2k.jpg",
                "pine_sapling_small/alpha.jpg",
            ),
        }
        for name, (color, alpha) in texture_inputs.items():
            prepare_texture(
                source / color,
                staging,
                f"Textures/{name}.png",
                source / alpha if alpha else None,
            )
        prepare_environment(
            source / "qwantani_dusk_2_puresky.hdr", staging / "Environments/dusk.hdr"
        )
        prepare_environment(
            source / "kloofendal_48d_partly_cloudy_puresky.hdr",
            staging / "Environments/day.hdr",
        )
        catalog = {
            "schema": "luxengine.rainy_creek.catalog.v1",
            "coordinateSystem": {"up": "Y", "units": "meters", "textureOrigin": "top-left"},
            "models": {},
            "textures": {"ground": "Textures/ground.png"},
            "environments": {"dusk": "Environments/dusk.hdr", "day": "Environments/day.hdr"},
            "environmentNotes": {
                "day": (
                    "Partly-cloudy daylight source; the original solar radiance is retained."
                )
            },
        }
        for model_id, relative, mesh_index, base_target, high_target, far_target in MODEL_SELECTION:
            document, accessor = read_gltf(source / relative)
            nodes = [node for node in document["nodes"] if node.get("mesh") == mesh_index]
            if len(nodes) != 1:
                raise AssetError("Each selected source mesh must have one reviewed scene node")
            node = nodes[0]
            if (
                "matrix" in node
                or node.get("rotation", [0, 0, 0, 1]) != [0, 0, 0, 1]
                or node.get("scale", [1, 1, 1]) != [1, 1, 1]
                or "children" in node
            ):
                raise AssetError("Selected source nodes must preserve identity rotation and scale")
            primitives = document["meshes"][mesh_index]["primitives"]
            source_triangles = sum(
                document["accessors"][part["indices"]]["count"] // 3
                for part in primitives
            )
            model = {
                "sourceMesh": mesh_index,
                "sourceTriangles": source_triangles,
                "removedLayoutTranslation": node.get("translation", [0, 0, 0]),
                "parts": [],
            }
            levels = (
                ("parts", base_target), ("highParts", high_target), ("farParts", far_target)
            )
            for level, target in levels:
                if target is None:
                    continue
                model[level] = []
                for part in primitives:
                    if part.get("mode", 4) != 4:
                        raise AssetError("Only triangle-list source geometry is accepted")
                    material = document["materials"][part["material"]]["name"]
                    texture, roughness, alpha_mask = MATERIALS[material]
                    attributes = part["attributes"]
                    positions = np.ascontiguousarray(
                        accessor(attributes["POSITION"]), dtype=np.float32
                    )
                    normals = np.ascontiguousarray(accessor(attributes["NORMAL"]), dtype=np.float32)
                    uvs = np.ascontiguousarray(accessor(attributes["TEXCOORD_0"]), dtype=np.float32)
                    indices = np.ascontiguousarray(
                        accessor(part["indices"]).reshape(-1), dtype=np.uint32
                    )
                    part_target = max(256, int(target * len(indices) / (source_triangles * 3)))
                    simplified, error = simplify(
                        positions, normals, uvs, indices, part_target, alpha_mask
                    )
                    suffix = "" if level == "parts" else "_high" if level == "highParts" else "_far"
                    part_suffix = f"_{texture.removeprefix('pine_')}" if len(primitives) > 1 else ""
                    mesh_path = f"Models/{model_id}{part_suffix}{suffix}.obj"
                    metrics = write_obj(staging / mesh_path, positions, normals, uvs, simplified)
                    model[level].append({
                        "mesh": mesh_path,
                        "albedo": f"Textures/{texture}.png",
                        "roughness": roughness,
                        "alphaMask": alpha_mask,
                        "alphaCutoff": 0.5 if alpha_mask else 0.0,
                        "twoSided": alpha_mask,
                        "simplificationError": error,
                        **metrics,
                    })
            catalog["models"][model_id] = model
        write_json(staging / "catalog.json", catalog)
        write_notice(staging / "LICENSE.txt", sources)
        outputs = [
            file_entry(path, staging)
            for path in sorted(staging.rglob("*"))
            if path.is_file()
        ]
        total = sum(entry["bytes"] for entry in outputs)
        if total > MAX_OUTPUT_BYTES:
            raise AssetError(
                f"Prepared asset budget exceeded: {total} > {MAX_OUTPUT_BYTES} bytes"
            )
        provenance = {
            "schema": "luxengine.rainy_creek.provenance.v1",
            "provider": sources["provider"],
            "license": sources["license"],
            "licenseUrl": sources["licenseUrl"],
            "assets": sources["assets"],
            "sourceFiles": sources["files"],
            "sourceRosterSha256": digest(SOURCE_ROSTER),
            "conversionScriptSha256": digest(Path(__file__)),
            "recipe": {
                "version": 1,
                "selection": [list(item) for item in MODEL_SELECTION],
                "textures": "1024 RGBA PNG, Lanczos; vegetation alpha threshold 0.5 to 0/255",
                "environments": "1024x512 Radiance HDR, area-filtered in linear radiance",
                "meshes": (
                    "OBJ Y-up meters; compact independent indices; original UVs/normals; "
                    "UV and PNG both preserve glTF top-left origin without vertical flips"
                ),
                "nodes": (
                    "Selected source nodes have identity rotation and scale; their variant "
                    "preview-grid translations are removed to preserve asset-local pivots."
                ),
                "simplification": (
                    "meshoptimizer attribute-aware, normals weight .15, UV weight .5; "
                    "relative error .012 opaque/.025 foliage; foliage pruning permitted"
                ),
                "tools": {
                    name: importlib.metadata.version(name)
                    for name in ("numpy", "Pillow", "meshoptimizer", "opencv-python")
                },
                "python": sys.version.split()[0],
            },
            "admission": {
                "method": "verified_local_source_conversion",
                "canonicalPackageCreated": False,
                "gameDevVersion": "1.5.1",
                "reason": (
                    "The canonical tool accepts GLB model packages; this runtime set "
                    "contains independently converted OBJ parts, PNG textures and "
                    "Radiance HDR environments."
                ),
                "originalEngineCodeCopied": False,
            },
            "files": outputs,
        }
        write_json(staging / "Provenance.json", provenance)
        verify(staging)
        shutil.move(str(staging), str(output))
    return verify(output)


def write_json(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def write_notice(path, sources):
    lines = [
        "Rainy Creek asset selection — Poly Haven", "", "License: CC0-1.0",
        "https://creativecommons.org/publicdomain/zero/1.0/", sources["licenseUrl"], "",
        "Poly Haven permits reuse, modification and redistribution, including commercial use.",
        "Independently converted from verified local Poly Haven glTF/JPEG/HDR data.",
        "No Atanara or Amina engine implementation, shaders or preparation code is included.", "",
    ]
    for asset in sources["assets"]:
        lines.append(f"{asset['id']} — {', '.join(asset['authors'])}")
        lines.append(f"https://polyhaven.com/a/{asset['id']}")
    lines.extend((
        "", "Original download URLs and hashes, transformation settings, tool versions and output hashes",
        "are recorded in Provenance.json. This is an independent verification record, not a canonical",
        "game-dev package receipt.", "",
    ))
    path.write_text("\n".join(lines), encoding="utf-8")


def file_entry(path, root):
    return {
        "path": path.relative_to(root).as_posix(),
        "bytes": path.stat().st_size,
        "sha256": digest(path),
    }


def verify_obj(path):
    positions = normals = uvs = faces = 0
    references = []
    with path.open(encoding="ascii") as stream:
        for line in stream:
            tokens = line.split()
            if not tokens or tokens[0].startswith("#"):
                continue
            kind, values = tokens[0], tokens[1:]
            if kind in ("v", "vn", "vt"):
                if len(values) != (2 if kind == "vt" else 3) or not all(
                    math.isfinite(float(v)) for v in values
                ):
                    raise AssetError(f"Invalid OBJ attribute: {path.name}")
                positions += kind == "v"
                normals += kind == "vn"
                uvs += kind == "vt"
            elif kind == "f":
                if len(values) != 3:
                    raise AssetError(f"Only triangulated output is accepted: {path.name}")
                faces += 1
                references.extend(tuple(int(i) for i in value.split("/")) for value in values)
            else:
                raise AssetError(f"Unexpected OBJ content: {path.name}")
    if positions == 0 or normals == 0 or uvs == 0 or faces == 0:
        raise AssetError(f"Incomplete OBJ attributes: {path.name}")
    if any(
        len(row) != 3 or any(
            i < 1 or i > maximum for i, maximum in zip(row, (positions, uvs, normals))
        )
        for row in references
    ):
        raise AssetError(f"Out-of-range OBJ index: {path.name}")
    return len(set(references)), faces


def verify_image_header(path):
    with path.open("rb") as stream:
        header = stream.read(256)
    if path.suffix == ".png":
        if header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
            raise AssetError(f"Invalid PNG header: {path.name}")
        if struct.unpack(">IIBBBBB", header[16:29]) != (1024, 1024, 8, 6, 0, 0, 0):
            raise AssetError(f"Expected a 1024x1024 RGBA PNG: {path.name}")
    elif path.suffix == ".hdr":
        if not header.startswith(b"#?RADIANCE\n") or b"-Y 512 +X 1024\n" not in header:
            raise AssetError(f"Expected a 1024x512 Radiance HDR: {path.name}")


def verify(root):
    root = root.absolute()
    if root.is_symlink() or not root.is_dir():
        raise AssetError("Asset output must be a directory and cannot be a symlink")
    provenance = json.loads(owned_file(root, "Provenance.json").read_text(encoding="utf-8"))
    if (
        provenance.get("schema") != "luxengine.rainy_creek.provenance.v1"
        or provenance.get("license") != "CC0-1.0"
    ):
        raise AssetError("Invalid provenance identity")
    if (
        provenance.get("sourceRosterSha256") != digest(SOURCE_ROSTER)
        or provenance.get("sourceFiles") != roster()["files"]
    ):
        raise AssetError("Source provenance differs from the reviewed roster")
    admission = provenance.get("admission", {})
    if (
        admission.get("canonicalPackageCreated") is not False
        or admission.get("originalEngineCodeCopied") is not False
    ):
        raise AssetError(
            "The independent conversion must not claim a canonical package or copied source"
        )
    expected = {"Provenance.json"}
    for entry in provenance["files"]:
        relative = entry["path"]
        path = owned_file(root, relative)
        if relative in expected or path.suffix not in OUTPUT_EXTENSIONS:
            raise AssetError(f"Unapproved output file: {relative}")
        if path.stat().st_size != entry["bytes"] or digest(path) != entry["sha256"]:
            raise AssetError(f"Output integrity check failed: {relative}")
        if path.suffix in (".png", ".hdr"):
            verify_image_header(path)
        expected.add(relative)
    actual = set()
    for path in root.rglob("*"):
        if path.is_symlink():
            raise AssetError("Output symlinks are not allowed")
        if path.is_file():
            actual.add(path.relative_to(root).as_posix())
    if actual != expected:
        raise AssetError("Output directory contains unrecorded files")
    catalog = json.loads(owned_file(root, "catalog.json").read_text(encoding="utf-8"))
    if (
        catalog.get("schema") != "luxengine.rainy_creek.catalog.v1"
        or set(catalog["models"]) != {item[0] for item in MODEL_SELECTION}
    ):
        raise AssetError("Catalog differs from the reviewed model selection")
    meshes = set()
    triangles = 0
    for model in catalog["models"].values():
        for level in ("parts", "highParts", "farParts"):
            for part in model.get(level, []):
                path = owned_file(root, part["mesh"])
                vertices, faces = verify_obj(path)
                if vertices != part["vertices"] or faces != part["triangles"]:
                    raise AssetError(f"Catalog mesh metrics differ: {part['mesh']}")
                if not 0.0 <= part["simplificationError"] <= (
                    0.0251 if part["alphaMask"] else 0.0121
                ):
                    raise AssetError("Simplification exceeded its silhouette error bound")
                if part["alphaMask"] and part["alphaCutoff"] != 0.5:
                    raise AssetError("Vegetation alpha cutoff differs from the source material")
                owned_file(root, part["albedo"])
                meshes.add(part["mesh"])
                triangles += faces
    for relative in (*catalog["textures"].values(), *catalog["environments"].values()):
        owned_file(root, relative)
    total = sum(path.stat().st_size for path in root.rglob("*") if path.is_file())
    if total > MAX_OUTPUT_BYTES:
        raise AssetError("Output exceeds the asset budget")
    return {
        "ok": True,
        "files": len(actual),
        "models": len(catalog["models"]),
        "meshes": len(meshes),
        "trianglesIncludingLODs": triangles,
        "bytes": total,
        "canonicalPackageCreated": False,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, help="Verified local Poly Haven input directory")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument(
        "--verify", action="store_true",
        help="Verify committed assets without conversion dependencies",
    )
    args = parser.parse_args()
    try:
        if not args.verify and args.source is None:
            parser.error("--source is required when preparing assets")
        result = verify(args.output) if args.verify else prepare(args.source, args.output)
        print(json.dumps(result, indent=2))
        return 0
    except (OSError, ValueError, KeyError, TypeError, ImportError) as error:
        print(f"Rainy Creek 에셋 처리에 실패했습니다: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
