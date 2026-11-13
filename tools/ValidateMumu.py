import argparse
import bisect
from collections import Counter
import json
import math
from pathlib import Path
import struct
import sys


FORMATS = {5120: ("b", 1), 5121: ("B", 1), 5122: ("h", 2),
           5123: ("H", 2), 5125: ("I", 4), 5126: ("f", 4)}
WIDTHS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}
IDENTITY = (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
PARTS = ("body", "face", "foot_l", "foot_r")
BOUNDS_TOLERANCE = 0.01


def require(condition, message):
    if not condition:
        raise ValueError(message)


def area_squared(a, b, c):
    u = [b[i] - a[i] for i in range(3)]
    v = [c[i] - a[i] for i in range(3)]
    cross = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2],
             u[0] * v[1] - u[1] * v[0])
    return sum(x * x for x in cross)


def load_metadata(directory):
    metadata = json.loads((directory / "Mumu.asset.json").read_text(encoding="utf-8"))
    lods = sorted(metadata["lods"], key=lambda lod: lod["level"])
    require([lod["level"] for lod in lods] == [0, 1, 2], "asset.json: LOD 0~2 정보 누락")
    for lod in lods:
        require(set(lod["parts"]) == set(PARTS), f"asset.json LOD{lod['level']}: 부품 목록 불일치")
        for key in ("vertices", "triangles"):
            require(sum(part[key] for part in lod["parts"].values()) == lod[key],
                    f"asset.json LOD{lod['level']}: 부품 {key} 합계 불일치")
    require(set(metadata["part_pivots"]) == set(PARTS), "asset.json: 부품 피벗 누락")
    animations = {a["name"]: a for a in metadata["animations"]}
    require(set(animations) == {"Idle", "Walk", "Hop"}, "asset.json: 애니메이션 3종 누락")
    return metadata, lods, animations


def validate_obj(path):
    vertices, faces, libraries, smoothing = [], [], [], []
    for line in path.read_text(encoding="utf-8").splitlines():
        fields = line.split()
        if not fields:
            continue
        if fields[0] == "v":
            vertices.append(tuple(map(float, fields[1:4])))
        elif fields[0] == "f":
            faces.append(tuple(int(x.split("/")[0]) - 1 for x in fields[1:]))
        elif fields[0] == "mtllib":
            libraries.extend(fields[1:])
        elif fields[0] == "s":
            smoothing.append(fields[1] if len(fields) > 1 else "off")
    require(vertices and faces, f"{path}: 빈 메시")
    require(smoothing and all(group not in ("0", "off") for group in smoothing),
            f"{path}: 스무딩 그룹(s 1)이 없어 엔진에서 면 단위 조명으로 표시됩니다")
    require(all(math.isfinite(x) for v in vertices for x in v), f"{path}: 유한하지 않은 정점")
    require(libraries and all((path.parent / p).is_file() for p in libraries), f"{path}: MTL 누락")
    edges, directions = Counter(), Counter()
    volume = 0
    for face in faces:
        require(len(face) == 3 and len(set(face)) == 3 and
                all(0 <= i < len(vertices) for i in face), f"{path}: 잘못된 면 인덱스")
        a, b, c = (vertices[i] for i in face)
        require(area_squared(a, b, c) > 1e-18, f"{path}: 퇴화 삼각형")
        volume += (a[0] * (b[1] * c[2] - b[2] * c[1]) +
                   a[1] * (b[2] * c[0] - b[0] * c[2]) +
                   a[2] * (b[0] * c[1] - b[1] * c[0]))
        for i, j in zip(face, face[1:] + face[:1]):
            edges[tuple(sorted((i, j)))] += 1
            directions[i, j] += 1
    require(all(v == 2 for v in edges.values()), f"{path}: 열린 면 또는 비다양체 모서리")
    require(all(directions[i, j] == directions[j, i] for i, j in edges), f"{path}: 면 방향 불일치")
    require(volume > 0, f"{path}: 안쪽을 향하는 메시")
    return vertices, faces


class Glb:
    def __init__(self, path):
        self.path = path
        data = path.read_bytes()
        require(len(data) >= 28, f"{path}: 잘린 GLB")
        magic, version, length = struct.unpack_from("<III", data)
        require(magic == 0x46546C67 and version == 2 and length == len(data), f"{path}: GLB 헤더 오류")
        chunks, offset = {}, 12
        while offset < len(data):
            size, kind = struct.unpack_from("<II", data, offset)
            require(size % 4 == 0 and offset + 8 + size <= len(data), f"{path}: 청크 범위 오류")
            chunks[kind] = data[offset + 8:offset + 8 + size]
            offset += 8 + size
        self.document = json.loads(chunks[0x4E4F534A])
        self.binary = chunks[0x004E4942]
        require(self.document["asset"]["version"] == "2.0", f"{path}: glTF 버전 오류")
        require(len(self.document["buffers"]) == 1 and
                0 <= len(self.binary) - self.document["buffers"][0]["byteLength"] <= 3,
                f"{path}: 버퍼 크기 오류")
        self.cache = {}
        for index in range(len(self.document["accessors"])):
            self.values(index)

    def values(self, index):
        if index in self.cache:
            return self.cache[index]
        accessor = self.document["accessors"][index]
        view = self.document["bufferViews"][accessor["bufferView"]]
        fmt, size = FORMATS[accessor["componentType"]]
        width = WIDTHS[accessor["type"]]
        stride = view.get("byteStride", width * size)
        offset = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
        end = offset + (accessor["count"] - 1) * stride + width * size
        require(accessor["count"] > 0 and stride >= width * size and
                end <= view.get("byteOffset", 0) + view["byteLength"] <= len(self.binary),
                f"{self.path}: accessor {index} 범위 오류")
        values = [struct.unpack_from("<" + fmt * width, self.binary, offset + i * stride)
                  for i in range(accessor["count"])]
        require(all(math.isfinite(x) for row in values for x in row), f"{self.path}: 유한하지 않은 accessor")
        self.cache[index] = values
        return values

    def validate(self, level, expected_triangles, animations):
        doc = self.document
        require(len(doc["meshes"]) == 4 and len(doc["materials"]) == 2, f"{self.path}: 부품 또는 재질 수 오류")
        triangle_count = 0
        for mesh in doc["meshes"]:
            for primitive in mesh["primitives"]:
                require(primitive.get("mode", 4) == 4, f"{self.path}: 삼각형이 아닌 메시")
                attrs = primitive["attributes"]
                vertices = self.values(attrs["POSITION"])
                normals = self.values(attrs["NORMAL"])
                indices = [v[0] for v in self.values(primitive["indices"])]
                require(len(normals) == len(vertices) and
                        all(abs(sum(x * x for x in n) - 1) < 1e-4 for n in normals),
                        f"{self.path}: 노멀 오류")
                require(len(indices) % 3 == 0 and all(0 <= i < len(vertices) for i in indices),
                        f"{self.path}: 메시 인덱스 오류")
                for i in range(0, len(indices), 3):
                    require(area_squared(*(vertices[k] for k in indices[i:i + 3])) > 1e-18,
                            f"{self.path}: 퇴화 삼각형")
                triangle_count += len(indices) // 3
                if level == 0:
                    joints = self.values(attrs["JOINTS_0"])
                    weights = self.values(attrs["WEIGHTS_0"])
                    require(len(joints) == len(weights) == len(vertices) and
                            all(0 <= x < 4 for row in joints for x in row) and
                            all(all(0 <= x <= 1 for x in row) and abs(sum(row) - 1) < 1e-5 for row in weights),
                            f"{self.path}: 스킨 가중치 오류")
                    require(all(abs(max(row) - 1) < 1e-6 for row in weights),
                            f"{self.path} {mesh.get('name')}: 정점 하나가 뼈 하나에 100% 묶이는 강체 스킨이 아닙니다")
        require(triangle_count == expected_triangles, f"{self.path}: 삼각형 수가 asset.json과 다릅니다")
        if level:
            require(not doc.get("skins") and not doc.get("animations"), f"{self.path}: 정적 LOD 구성 오류")
        else:
            skins = doc["skins"]
            require(len(skins) == 1 and len(skins[0]["joints"]) == 4 and
                    {doc["nodes"][i]["name"] for i in skins[0]["joints"]} == {"Root", "Body", "Foot.L", "Foot.R"},
                    f"{self.path}: 4개 뼈 구성 오류")
            self.validate_animations(animations)
        print(f"{self.path.name}: {triangle_count:,}개 삼각형, 메시·재질·버퍼 정상")

    def validate_animations(self, animations):
        durations = {name: a["duration_seconds"] for name, a in animations.items()}
        require({a["name"] for a in self.document["animations"]} == set(durations), "애니메이션 3종 누락")
        for animation in self.document["animations"]:
            changed = False
            for channel in animation["channels"]:
                sampler = animation["samplers"][channel["sampler"]]
                times = [x[0] for x in self.values(sampler["input"])]
                values = self.values(sampler["output"])
                require(sampler.get("interpolation", "LINEAR") in ("LINEAR", "STEP") and
                        len(times) == len(values) and abs(times[0]) < 1e-6 and
                        abs(times[-1] - durations[animation["name"]]) < 1e-5 and
                        all(a < b for a, b in zip(times, times[1:])), "키프레임 시간 오류")
                changed |= any(max(row[k] for row in values) - min(row[k] for row in values) > 1e-5
                               for k in range(len(values[0])))
                if animations[animation["name"]]["loop"]:
                    first, last = values[0], values[-1]
                    if channel["target"]["path"] == "rotation" and sum(a * b for a, b in zip(first, last)) < 0:
                        last = tuple(-x for x in last)
                    require(max(abs(a - b) for a, b in zip(first, last)) < 1e-4,
                            f"{animation['name']}: 반복 애니메이션의 첫·마지막 자세가 달라 이음새가 튑니다 "
                            f"(노드 {channel['target']['node']} {channel['target']['path']})")
            require(changed, f"{animation['name']}: 변화가 없는 애니메이션")
            loop = "반복 이음새 정상" if animations[animation["name"]]["loop"] else "1회 재생"
            print(f"  {animation['name']}: {durations[animation['name']]}초, 실제 변형 확인, {loop}")


def multiply(a, b):
    return tuple(sum(a[r * 4 + k] * b[k * 4 + c] for k in range(4))
                 for r in range(4) for c in range(4))


def trs(translation, rotation, scale):
    x, y, z, w = rotation
    return ((1 - 2 * (y * y + z * z)) * scale[0], 2 * (x * y - z * w) * scale[1], 2 * (x * z + y * w) * scale[2], translation[0],
            2 * (x * y + z * w) * scale[0], (1 - 2 * (x * x + z * z)) * scale[1], 2 * (y * z - x * w) * scale[2], translation[1],
            2 * (x * z - y * w) * scale[0], 2 * (y * z + x * w) * scale[1], (1 - 2 * (x * x + y * y)) * scale[2], translation[2],
            0, 0, 0, 1)


def interpolate(times, values, time, rotation=False, step=False):
    i = max(0, min(len(times) - 2, bisect.bisect_right(times, time) - 1))
    t = 0 if step else max(0, min(1, (time - times[i]) / (times[i + 1] - times[i])))
    a, b = values[i], values[i + 1]
    if rotation:
        dot = sum(x * y for x, y in zip(a, b))
        if dot < 0:
            b, dot = tuple(-x for x in b), -dot
        if dot < 0.9995:
            angle = math.acos(max(-1, min(1, dot)))
            return tuple((math.sin((1 - t) * angle) * x + math.sin(t * angle) * y) / math.sin(angle)
                         for x, y in zip(a, b))
    result = tuple((1 - t) * x + t * y for x, y in zip(a, b))
    if rotation:
        length = math.sqrt(sum(x * x for x in result))
        result = tuple(x / length for x in result)
    return result


def foot_clearance(glb):
    doc = glb.document
    parents = {child: i for i, node in enumerate(doc["nodes"]) for child in node.get("children", [])}
    skin = doc["skins"][0]
    inverses = [tuple(m[c * 4 + r] for r in range(4) for c in range(4))
                for m in glb.values(skin["inverseBindMatrices"])]
    feet = []
    for mesh in doc["meshes"]:
        if "foot_" in mesh["name"]:
            attrs = mesh["primitives"][0]["attributes"]
            vertices = glb.values(attrs["POSITION"])
            joints = glb.values(attrs["JOINTS_0"])
            weights = glb.values(attrs["WEIGHTS_0"])
            require(all(max(w) == 1 for w in weights), "발바닥 검증은 강체 스킨을 전제합니다")
            feet.append([(v, j[w.index(1)]) for v, j, w in zip(vertices, joints, weights)])
    require(len(feet) == 2, "발 메시 2개를 찾지 못해 발바닥 검증을 할 수 없습니다")
    for animation in doc["animations"]:
        channels = []
        duration = 0
        for channel in animation["channels"]:
            sampler = animation["samplers"][channel["sampler"]]
            times = [x[0] for x in glb.values(sampler["input"])]
            duration = max(duration, times[-1])
            channels.append((channel["target"]["node"], channel["target"]["path"], times,
                             glb.values(sampler["output"]), sampler.get("interpolation") == "STEP"))
        minimum = math.inf
        for sample in range(math.ceil(duration * 120) + 1):
            time = min(duration, sample / 120)
            transforms = [{"translation": n.get("translation", (0, 0, 0)),
                           "rotation": n.get("rotation", (0, 0, 0, 1)),
                           "scale": n.get("scale", (1, 1, 1))} for n in doc["nodes"]]
            for node, path, times, values, step in channels:
                transforms[node][path] = interpolate(times, values, time, path == "rotation", step)
            worlds = {}
            def world(index):
                if index not in worlds:
                    n = transforms[index]
                    local = trs(n["translation"], n["rotation"], n["scale"])
                    worlds[index] = multiply(world(parents[index]), local) if index in parents else local
                return worlds[index]
            matrices = [multiply(world(j), inverse) for j, inverse in zip(skin["joints"], inverses)]
            for foot in feet:
                for vertex, joint in foot:
                    m = matrices[joint]
                    minimum = min(minimum, sum(m[4 + k] * vertex[k] for k in range(3)) + m[7])
        print(f"  {animation['name']} 발바닥 최저점: {minimum:.6f}m (120Hz 표본)")
        require(minimum >= -0.001, f"{animation['name']}: 발바닥이 지면을 1mm 이상 관통합니다")


def main():
    parser = argparse.ArgumentParser(description="무무 3D 모델의 기하·스킨·애니메이션 검증")
    parser.add_argument("directory", nargs="?", type=Path,
                        default=Path(__file__).resolve().parents[1] / "Engine" / "Resources" / "Characters" / "Mumu")
    args = parser.parse_args()
    metadata, lods, animations = load_metadata(args.directory)
    for lod in lods:
        level = lod["level"]
        count = 0
        low, high = [math.inf] * 3, [-math.inf] * 3
        for part in PARTS:
            vertices, faces = validate_obj(args.directory / "luxengine" / f"lod{level}" / f"{part}.obj")
            expected = lod["parts"][part]
            require(len(vertices) == expected["vertices"] and len(faces) == expected["triangles"],
                    f"OBJ LOD{level} {part}: 정점·삼각형 수가 asset.json과 다릅니다 "
                    f"({len(vertices)}/{len(faces)}, 기대 {expected['vertices']}/{expected['triangles']})")
            pivot = metadata["part_pivots"][part] if part.startswith("foot") else (0, 0, 0)
            for vertex in vertices:
                for axis in range(3):
                    low[axis] = min(low[axis], vertex[axis] + pivot[axis])
                    high[axis] = max(high[axis], vertex[axis] + pivot[axis])
            count += len(faces)
        if level == 0:
            bounds = metadata["bounds"]
            require(all(abs(low[i] - bounds["min"][i]) <= BOUNDS_TOLERANCE and
                        abs(high[i] - bounds["max"][i]) <= BOUNDS_TOLERANCE for i in range(3)),
                    f"OBJ LOD0: 경계 {low}~{high}가 asset.json 경계와 다릅니다")
        print(f"OBJ LOD{level}: 부품 4개, {count:,}개 삼각형, asset.json·닫힌 면·면 방향·MTL·스무딩 정상")
    for lod, name in zip(lods, ("Mumu.glb", "Mumu_LOD1.glb", "Mumu_LOD2.glb")):
        glb = Glb(args.directory / name)
        glb.validate(lod["level"], lod["triangles"], animations)
        lod = lod["level"]
        if lod == 0:
            foot_clearance(glb)
    print("무무 모델 검증 완료: OBJ 12종, GLB 3종, 애니메이션 3종 통과")


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    try:
        main()
    except (ValueError, OSError, KeyError, IndexError, TypeError, struct.error) as error:
        raise SystemExit(f"무무 모델 검증 실패: {error}")
