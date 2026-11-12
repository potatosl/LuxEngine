import argparse
import json
import math
from pathlib import Path
import tempfile

import bmesh
import bpy
import numpy as np
from mathutils import Vector


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "Assets" / "Characters" / "Mumu"
BODY_CENTER = 0.94
BODY_RADII = (1.0, 0.64, 0.79)
FOOT_PIVOTS = {"foot_l": (-0.53, 0.0, 0.10), "foot_r": (0.53, 0.0, 0.10)}
WHITE = (0.82, 0.81, 0.76, 1.0)
INK = (0.008, 0.009, 0.012, 1.0)


def material(name, color, roughness):
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = color
    mat.use_nodes = True
    shader = mat.node_tree.nodes.get("Principled BSDF")
    shader.inputs["Base Color"].default_value = color
    shader.inputs["Roughness"].default_value = roughness
    return mat


def mochi_material():
    size = 1024
    rng = np.random.default_rng(240711)
    frequencies = np.fft.fftfreq(size)
    radius_squared = frequencies[:, None] ** 2 + frequencies[None, :] ** 2
    source = np.fft.fft2(rng.standard_normal((size, size)))

    def noise(sigma):
        field = np.fft.ifft2(source * np.exp(-2 * math.pi ** 2 * sigma ** 2 * radius_squared)).real
        return (field - field.mean()) / field.std()

    fine = noise(0.65)
    soft = noise(5.0)
    height = 0.85 * fine + 0.15 * soft
    dx = (np.roll(height, -1, 1) - np.roll(height, 1, 1)) * 0.045
    dy = (np.roll(height, -1, 0) - np.roll(height, 1, 0)) * 0.045
    normals = np.stack((-dx, -dy, np.ones_like(dx)), axis=-1)
    normals /= np.linalg.norm(normals, axis=-1, keepdims=True)
    roughness = np.clip(0.84 + 0.028 * fine + 0.012 * soft, 0.74, 0.94)
    folder = OUT / "textures"
    folder.mkdir(parents=True, exist_ok=True)

    def texture(name, pixels):
        image = bpy.data.images.new(name, width=size, height=size, alpha=False)
        image.colorspace_settings.name = "Non-Color"
        rgba = np.ones((size, size, 4), dtype=np.float32)
        rgba[:, :, :3] = pixels
        image.pixels.foreach_set(rgba.ravel())
        image.filepath_raw = str(folder / f"{name}.png")
        image.file_format = "PNG"
        image.save()
        image.pack()
        return image

    normal_image = texture("Mochi_Normal", normals * 0.5 + 0.5)
    roughness_image = texture("Mochi_Roughness", roughness[:, :, None])
    mat = material("Mumu_Mochi", WHITE, 0.84)
    shader = mat.node_tree.nodes.get("Principled BSDF")
    shader.inputs["IOR"].default_value = 1.36
    shader.inputs["Specular IOR Level"].default_value = 0.22
    shader.inputs["Diffuse Roughness"].default_value = 0.35
    shader.inputs["Subsurface Weight"].default_value = 0.12
    shader.inputs["Subsurface Radius"].default_value = (1.0, 0.65, 0.4)
    shader.inputs["Subsurface Scale"].default_value = 0.025
    shader.location = (160, 60)
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    normal_texture = nodes.new("ShaderNodeTexImage")
    normal_texture.image = normal_image
    normal_texture.location = (-520, -180)
    normal_map = nodes.new("ShaderNodeNormalMap")
    normal_map.inputs["Strength"].default_value = 0.45
    normal_map.location = (-140, -180)
    links.new(normal_texture.outputs["Color"], normal_map.inputs["Color"])
    links.new(normal_map.outputs["Normal"], shader.inputs["Normal"])
    roughness_texture = nodes.new("ShaderNodeTexImage")
    roughness_texture.image = roughness_image
    roughness_texture.location = (-520, 160)
    links.new(roughness_texture.outputs["Color"], shader.inputs["Roughness"])
    mat["finish"] = "mochi_powder"
    return mat


def mesh_object(name, verts, faces, mat):
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(mesh)
    bm.free()
    for poly in mesh.polygons:
        poly.use_smooth = True
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    mesh.materials.append(mat)
    return obj


def signed_power(value, exponent):
    return math.copysign(abs(value) ** exponent, value)


def blob(name, radii, center, segments, rings, mat, exponent=1.0):
    verts = [(center[0], center[1], center[2] - radii[2])]
    for j in range(1, rings):
        latitude = -math.pi / 2 + math.pi * j / rings
        horizontal = abs(math.cos(latitude)) ** exponent
        for i in range(segments):
            longitude = 2 * math.pi * i / segments
            verts.append((
                center[0] + radii[0] * horizontal * signed_power(math.cos(longitude), 0.95),
                center[1] + radii[1] * horizontal * signed_power(math.sin(longitude), 0.95),
                center[2] + radii[2] * signed_power(math.sin(latitude), exponent),
            ))
    top = len(verts)
    verts.append((center[0], center[1], center[2] + radii[2]))
    faces = []
    for i in range(segments):
        faces.append((0, 1 + (i + 1) % segments, 1 + i))
    for j in range(rings - 2):
        start = 1 + j * segments
        nxt = start + segments
        for i in range(segments):
            a, b = start + i, start + (i + 1) % segments
            c, d = nxt + (i + 1) % segments, nxt + i
            faces.extend(((a, b, c), (a, c, d)))
    last = 1 + (rings - 2) * segments
    for i in range(segments):
        faces.append((last + i, last + (i + 1) % segments, top))
    obj = mesh_object(name, verts, faces, mat)
    uv = obj.data.uv_layers.new(name="UVMap")
    for poly in obj.data.polygons:
        values = []
        for loop_index in poly.loop_indices:
            co = obj.data.vertices[obj.data.loops[loop_index].vertex_index].co
            delta = co - Vector(center)
            u = (math.atan2(delta.y / radii[1], delta.x / radii[0]) / (2 * math.pi)) % 1.0
            v = math.asin(max(-1, min(1, delta.z / radii[2]))) / math.pi + 0.5
            values.append([loop_index, u, v])
        seam = max(v[1] for v in values) - min(v[1] for v in values) > 0.5
        for loop_index, u, v in values:
            uv.data[loop_index].uv = (u + (1 if seam and u < 0.5 else 0), v)
    return obj


def front_point(x, z):
    vertical = signed_power((z - BODY_CENTER) / BODY_RADII[2], 1 / 0.90)
    horizontal = max(0.0001, 1 - vertical * vertical) ** (0.90 / 2)
    longitude_cos = min(0.9999, abs(x / horizontal)) ** (1 / 0.95)
    y = -BODY_RADII[1] * horizontal * max(0, 1 - longitude_cos ** 2) ** (0.95 / 2)
    return Vector((x, y - 0.014, z))


def rounded_path(points, closed=False):
    points = [Vector(p) for p in points]
    result = []
    for i, current in enumerate(points):
        if not closed and i in (0, len(points) - 1):
            result.append(current)
            continue
        prev = points[(i - 1) % len(points)]
        nxt = points[(i + 1) % len(points)]
        cut = min(0.025, (prev - current).length * 0.23, (nxt - current).length * 0.23)
        a = current + (prev - current).normalized() * cut
        b = current + (nxt - current).normalized() * cut
        for t in (0, 0.25, 0.5, 0.75, 1):
            result.append((1 - t) ** 2 * a + 2 * (1 - t) * t * current + t ** 2 * b)
    dense = []
    count = len(result) if closed else len(result) - 1
    for i in range(count):
        a, b = result[i], result[(i + 1) % len(result)]
        steps = max(1, math.ceil((b - a).length / 0.045))
        dense.extend(a.lerp(b, j / steps) for j in range(steps))
    if not closed:
        dense.append(result[-1])
    return [front_point(p.x, p.y) for p in dense]


def face_object(radial, mat, name):
    strokes = [
        ([(-0.74, 1.205), (-0.24, 1.235)], False, 0.018),
        ([(0.24, 1.235), (0.74, 1.205)], False, 0.018),
        ([(-0.595, 1.215), (-0.515, 1.045), (-0.405, 1.225)], False, 0.016),
        ([(0.405, 1.225), (0.515, 1.045), (0.595, 1.215)], False, 0.016),
        ([(-0.066, 1.005), (0.066, 1.005)], False, 0.016),
        ([(0, 0.860), (-0.156, 0.617), (0.156, 0.617)], True, 0.0175),
    ]
    verts, faces = [], []
    for points, closed, radius in strokes:
        path = rounded_path(points, closed)
        frames = []
        for i, p in enumerate(path):
            if closed:
                tangent = (path[(i + 1) % len(path)] - path[(i - 1) % len(path)]).normalized()
            elif i == 0:
                tangent = (path[1] - p).normalized()
            elif i == len(path) - 1:
                tangent = (p - path[i - 1]).normalized()
            else:
                tangent = (path[i + 1] - path[i - 1]).normalized()
            side = tangent.cross(Vector((0, -1, 0))).normalized()
            normal = side.cross(tangent).normalized()
            frames.append((tangent, side, normal))
        start = len(verts)
        for p, (_, side, normal) in zip(path, frames):
            for j in range(radial):
                a = 2 * math.pi * j / radial
                verts.append(tuple(p + radius * (math.cos(a) * side + math.sin(a) * normal)))
        for i in range(len(path) if closed else len(path) - 1):
            a, b = start + i * radial, start + ((i + 1) % len(path)) * radial
            for j in range(radial):
                jn = (j + 1) % radial
                faces.extend(((a + j, a + jn, b + jn), (a + j, b + jn, b + j)))
        if not closed:
            for endpoint, direction in ((0, -1), (len(path) - 1, 1)):
                tangent, side, normal = frames[endpoint]
                previous = start + endpoint * radial
                for cap in (1, 2):
                    angle = cap * math.pi / 6
                    ring = len(verts)
                    for j in range(radial):
                        a = 2 * math.pi * j / radial
                        verts.append(tuple(path[endpoint] + tangent * direction * radius * math.sin(angle)
                                           + radius * math.cos(angle) * (math.cos(a) * side + math.sin(a) * normal)))
                    for j in range(radial):
                        jn = (j + 1) % radial
                        faces.extend(((previous + j, previous + jn, ring + jn),
                                      (previous + j, ring + jn, ring + j)))
                    previous = ring
                tip = len(verts)
                verts.append(tuple(path[endpoint] + tangent * direction * radius))
                for j in range(radial):
                    faces.append((previous + j, previous + (j + 1) % radial, tip))
    return mesh_object(name, verts, faces, mat)


def make_lod(level, white, ink):
    segments, rings, foot_segments, foot_rings, radial = (
        (64, 32, 32, 14, 8), (36, 20, 24, 10, 6), (24, 12, 16, 8, 4)
    )[level]
    parts = {
        "body": blob(f"Mumu_Body_LOD{level}", BODY_RADII, (0, 0, BODY_CENTER), segments, rings, white, 0.90),
        "face": face_object(radial, ink, f"Mumu_Face_LOD{level}"),
    }
    for key, pivot in FOOT_PIVOTS.items():
        obj = blob(f"Mumu_{key}_LOD{level}", (0.285, 0.36, 0.10), (0, -0.045, 0),
                   foot_segments, foot_rings, white)
        obj.location = pivot
        parts[key] = obj
    return parts


def export_obj(path, objects, local=False):
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = ["mtllib Mumu.mtl"]
    offset = 1
    for key, obj in objects.items():
        # Smoothing group 1 lets LuxEngine generate shared vertex normals instead of flat faces.
        lines.extend((f"o {key}", f"usemtl {'Ink' if key == 'face' else 'Cream'}", "s 1"))
        for vertex in obj.data.vertices:
            co = vertex.co if local else obj.matrix_world @ vertex.co
            lines.append(f"v {co.x:.7f} {co.z:.7f} {-co.y:.7f}")
        obj.data.calc_loop_triangles()
        for tri in obj.data.loop_triangles:
            lines.append("f " + " ".join(str(i + offset) for i in tri.vertices))
        offset += len(obj.data.vertices)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def create_rig(parts):
    data = bpy.data.armatures.new("Mumu_Rig")
    arm = bpy.data.objects.new("Mumu_Rig", data)
    bpy.context.collection.objects.link(arm)
    bpy.context.view_layer.objects.active = arm
    arm.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    for name, pos, length, parent in (
        ("Root", (0, 0, 0), 0.2, None),
        ("Body", (0, 0, BODY_CENTER), 0.45, "Root"),
        ("Foot.L", FOOT_PIVOTS["foot_l"], 0.2, "Root"),
        ("Foot.R", FOOT_PIVOTS["foot_r"], 0.2, "Root"),
    ):
        bone = data.edit_bones.new(name)
        bone.head = pos
        bone.tail = Vector(pos) + Vector((0, 0, length))
        if parent:
            bone.parent = data.edit_bones[parent]
    bpy.ops.object.mode_set(mode="OBJECT")
    arm.show_in_front = True
    for key, obj in parts.items():
        bone = {"body": "Body", "face": "Body", "foot_l": "Foot.L", "foot_r": "Foot.R"}[key]
        group = obj.vertex_groups.new(name=bone)
        group.add(list(range(len(obj.data.vertices))), 1.0, "REPLACE")
        modifier = obj.modifiers.new("Mumu_Skin", "ARMATURE")
        modifier.object = arm
        obj.parent = arm
    return arm


def reset_pose(arm):
    for bone in arm.pose.bones:
        bone.rotation_mode = "XYZ"
        bone.location = (0, 0, 0)
        bone.rotation_euler = (0, 0, 0)
        bone.scale = (1, 1, 1)


def animate(arm, parts):
    clips = []
    bpy.context.preferences.edit.keyframe_new_interpolation_type = "LINEAR"
    for name, end in (("Idle", 61), ("Walk", 25), ("Hop", 37)):
        action = bpy.data.actions.new(name)
        arm.animation_data_create()
        arm.animation_data.action = action
        for frame in range(1, end + 1):
            reset_pose(arm)
            t = (frame - 1) / (end - 1)
            phase = math.tau * t
            root = arm.pose.bones["Root"]
            body = arm.pose.bones["Body"]
            if name == "Idle":
                s = math.sin(phase)
                body.location.y = 0.009 * s
                body.scale = (1 + 0.009 * s, 1 - 0.012 * s, 1 + 0.009 * s)
                body.rotation_euler.z = 0.012 * math.sin(phase)
            elif name == "Walk":
                body.location.y = 0.027 * (1 - math.cos(2 * phase))
                body.rotation_euler.z = 0.055 * math.sin(phase)
                body.rotation_euler.y = 0.035 * math.sin(phase)
                for bone_name, shift in (("Foot.L", 0), ("Foot.R", math.pi)):
                    bone = arm.pose.bones[bone_name]
                    p = phase + shift
                    bone.location.y = 0.14 * max(0, math.sin(p))
                    bone.location.z = 0.13 * math.cos(p)
                    bone.rotation_euler.x = 0.19 * max(0, math.sin(p))
                    key = "foot_l" if bone_name == "Foot.L" else "foot_r"
                    angle = bone.rotation_euler.x
                    minimum = min(v.co.y * math.sin(angle) + v.co.z * math.cos(angle)
                                  for v in parts[key].data.vertices)
                    bone.location.y += max(0, -0.10 - minimum)
            else:
                jump = max(0, math.sin(math.pi * min(1, max(0, (t - 0.18) / 0.64))))
                root.location.y = 0.48 * jump
                squash = -0.12 * math.sin(math.pi * t / 0.18) if t < 0.18 else (
                    -0.10 * math.sin(math.pi * (t - 0.82) / 0.18) if t > 0.82 else 0.06 * jump)
                body.scale = (1 - squash * 0.5, 1 + squash, 1 - squash * 0.5)
                body.location.y = BODY_RADII[2] * squash
                for bone_name in ("Foot.L", "Foot.R"):
                    arm.pose.bones[bone_name].rotation_euler.x = -0.20 * jump
            for bone in arm.pose.bones:
                for channel in ("location", "rotation_euler", "scale"):
                    bone.keyframe_insert(channel, frame=frame, group=bone.name)
        slot = arm.animation_data.action_slot
        track = arm.animation_data.nla_tracks.new()
        track.name = name
        strip = track.strips.new(name, 1, action)
        strip.action_slot = slot
        strip.use_auto_blend = False
        track.mute = True
        clips.append({"name": name, "duration_seconds": (end - 1) / 30, "loop": name != "Hop"})
        arm.animation_data.action = None
    reset_pose(arm)
    return clips


def select_only(objects):
    bpy.ops.object.select_all(action="DESELECT")
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]


def export_glb(path, parts, arm=None):
    select_only(([arm] if arm else []) + list(parts.values()))
    for obj in parts.values():
        obj.hide_set(False)
        obj.hide_render = False
    kwargs = dict(
        filepath=str(path), export_format="GLB", use_selection=True,
        export_yup=True, export_materials="EXPORT", export_normals=True,
        export_tangents=True,
        export_animations=bool(arm), export_cameras=False, export_lights=False,
    )
    if arm:
        kwargs.update(export_animation_mode="NLA_TRACKS", export_nla_strips=True,
                      export_frame_range=False, export_force_sampling=True,
                      export_anim_slide_to_zero=True, export_def_bones=True)
    bpy.ops.export_scene.gltf(**kwargs)


def point_at(obj, point):
    obj.rotation_euler = (Vector(point) - obj.location).to_track_quat("-Z", "Y").to_euler()


def setup_studio():
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 48
    scene.cycles.use_denoising = True
    scene.render.resolution_x = 1200
    scene.render.resolution_y = 1200
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.view_settings.view_transform = "AgX"
    scene.world.color = (0.18, 0.18, 0.18)
    world = scene.world
    world.use_nodes = True
    world.node_tree.nodes.get("Background").inputs[0].default_value = (0.28, 0.34, 0.40, 1)
    world.node_tree.nodes.get("Background").inputs[1].default_value = 0.30
    floor_mat = material("Studio_Backdrop", (0.125, 0.19, 0.21, 1), 0.85)
    bpy.ops.mesh.primitive_plane_add(size=200, location=(0, 0, -0.005))
    floor = bpy.context.object
    floor.name = "Studio_Floor"
    floor.data.materials.append(floor_mat)
    for name, location, power, size, color in (
        ("Studio_Key", (-3, -4, 6), 650, 4.5, (1.0, 0.90, 0.76)),
        ("Studio_Fill", (4, -2, 3), 320, 4.0, (0.76, 0.88, 1.0)),
        ("Studio_Rim", (1.5, 3, 4), 800, 3.0, (0.84, 0.95, 1.0)),
    ):
        data = bpy.data.lights.new(name, "AREA")
        data.energy = power
        data.shape = "DISK"
        data.size = size
        data.color = color
        light = bpy.data.objects.new(name, data)
        scene.collection.objects.link(light)
        light.location = location
        point_at(light, (0, 0, 0.8))
    data = bpy.data.cameras.new("Studio_Camera")
    camera = bpy.data.objects.new("Studio_Camera", data)
    scene.collection.objects.link(camera)
    data.type = "ORTHO"
    data.ortho_scale = 2.85
    scene.camera = camera
    return camera


def render(path, camera, location, target=(0, 0, 0.86), size=1200):
    camera.location = location
    point_at(camera, target)
    scene = bpy.context.scene
    scene.render.resolution_x = size
    scene.render.resolution_y = size
    scene.render.filepath = str(path)
    bpy.ops.render.render(write_still=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--skip-render", action="store_true")
    parser.add_argument("--animate-preview", action="store_true")
    import sys
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])
    export_temp = ROOT / "build" / "mumu-export-temp"
    export_temp.mkdir(parents=True, exist_ok=True)
    tempfile.tempdir = str(export_temp)
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "previews").mkdir(exist_ok=True)
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    scene = bpy.context.scene
    scene.render.fps = 30
    white = mochi_material()
    ink = material("Mumu_Ink", INK, 0.62)
    lods = []
    stats = []
    for level in range(3):
        parts = make_lod(level, white, ink)
        bpy.context.view_layer.update()
        lods.append(parts)
        for key, obj in parts.items():
            export_obj(OUT / "luxengine" / f"lod{level}" / f"{key}.obj", {key: obj}, local=key.startswith("foot"))
        folder = OUT / "exchange"
        folder.mkdir(exist_ok=True)
        export_obj(folder / f"Mumu_LOD{level}.obj", parts)
        triangles = sum(len(obj.data.polygons) for obj in parts.values())
        vertices = sum(len(obj.data.vertices) for obj in parts.values())
        stats.append({"level": level, "vertices": vertices, "triangles": triangles,
                      "parts": {k: {"vertices": len(v.data.vertices), "triangles": len(v.data.polygons)} for k, v in parts.items()}})
        if level:
            export_glb(OUT / f"Mumu_LOD{level}.glb", parts)
        for obj in parts.values():
            obj.hide_render = True
            obj.hide_set(True)
    mtl = "newmtl Cream\nKd 0.916 0.912 0.886\nKs 0.015 0.015 0.015\nNs 4\nPr 0.84\n\nnewmtl Ink\nKd 0.086 0.093 0.112\nKs 0.02 0.02 0.02\nNs 12\nPr 0.62\n"
    for folder in [OUT / "exchange"] + [OUT / "luxengine" / f"lod{i}" for i in range(3)]:
        (folder / "Mumu.mtl").write_text(mtl, encoding="utf-8")
    parts = lods[0]
    for obj in parts.values():
        obj.hide_set(False)
        obj.hide_render = False
    arm = create_rig(parts)
    clips = animate(arm, parts)
    scene.frame_start = 1
    scene.frame_end = 61
    scene.frame_set(1)
    export_glb(OUT / "Mumu.glb", parts, arm)
    for level in (1, 2):
        for obj in lods[level].values():
            obj.hide_render = True
            obj.hide_set(True)
    for obj in parts.values():
        obj.hide_set(False)
        obj.hide_render = False
    camera = setup_studio()
    camera.location = (3.1, -7, 2.7)
    point_at(camera, (0, 0, 0.86))
    if arm.animation_data.nla_tracks:
        arm.animation_data.action = bpy.data.actions["Idle"]
        arm.animation_data.action_slot = bpy.data.actions["Idle"].slots[0]
    scene.frame_set(1)
    select_only([parts["body"]])
    for area in bpy.context.screen.areas:
        if area.type == "VIEW_3D":
            area.spaces.active.region_3d.view_distance = 4.0
            area.spaces.active.region_3d.view_location = (0, 0, 0.85)
            area.spaces.active.region_3d.view_rotation = camera.rotation_euler.to_quaternion()
            area.spaces.active.shading.type = "MATERIAL"
            area.spaces.active.overlay.show_overlays = False
    bpy.context.preferences.filepaths.save_version = 0
    bpy.context.preferences.filepaths.file_preview_type = "NONE"
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "Mumu.blend"))
    metadata = {
        "name": "Mumu", "display_name": "무무", "generator": "Tools/BuildMumu.py",
        "units": "meter", "up_axis": "+Y", "forward_axis": "+Z", "origin": "ground_between_feet",
        "bounds": {"min": [-1, 0, -0.64], "max": [1, 1.73, 0.67]},
        "materials_linear_rgba": {"body": WHITE, "face": INK, "foot_l": WHITE, "foot_r": WHITE},
        "surface": {"finish": "mochi_powder", "roughness": 0.84, "normal_strength": 0.45,
                    "subsurface_weight_blender": 0.12, "subsurface_scale_m_blender": 0.025,
                    "textures": ["textures/Mochi_Normal.png", "textures/Mochi_Roughness.png"],
                    "textures_embedded_in_blend_and_glb": True, "glb_subsurface_supported": False},
        "part_pivots": {"body": [0, 0, 0], "face": [0, 0, 0],
                        **{k: [p[0], p[2], -p[1]] for k, p in FOOT_PIVOTS.items()}},
        "lods": stats, "rig": {"bones": ["Root", "Body", "Foot.L", "Foot.R"], "weighting": "rigid_one_bone_per_vertex"},
        "animations": clips,
        "suggested_collider": {"type": "capsule", "center": [0, 0.84, 0], "radius": 0.61, "total_height": 1.68},
        "luxengine": {"format": "obj", "parts": ["body", "face", "foot_l", "foot_r"],
                      "skeletal_animation_import": False, "texture_required": False},
    }
    (OUT / "Mumu.asset.json").write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if not args.skip_render:
        render(OUT / "previews" / "Mumu_Hero.png", camera, (3.1, -7, 2.7))
        (OUT / "previews" / "Mumu_Mochi.png").write_bytes((OUT / "previews" / "Mumu_Hero.png").read_bytes())
        render(OUT / "previews" / "Mumu_Front.png", camera, (0, -8, 1.5))
        render(OUT / "previews" / "Mumu_Side.png", camera, (8, 0, 1.5))
        render(OUT / "previews" / "Mumu_Back.png", camera, (0, 8, 1.5))
        camera.data.ortho_scale = 1.15
        render(OUT / "previews" / "Mumu_Mochi_Closeup.png", camera, (0.3, -8, 1.9), target=(0, 0, 1.03))
        camera.data.ortho_scale = 2.85
    if args.animate_preview:
        scene.cycles.samples = 16
        arm.animation_data.action = bpy.data.actions["Walk"]
        arm.animation_data.action_slot = bpy.data.actions["Walk"].slots[0]
        frames = ROOT / "build" / "mumu-frames"
        frames.mkdir(parents=True, exist_ok=True)
        for frame in range(1, 25):
            scene.frame_set(frame)
            render(frames / f"{frame:04d}.png", camera, (3.1, -7, 2.7), size=512)
    print("MUMU_BUILD_COMPLETE " + json.dumps(stats))


if __name__ == "__main__":
    main()
