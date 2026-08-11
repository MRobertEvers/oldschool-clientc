#!/usr/bin/env python3
"""Render the high-poly counter-class corpus (Class 2) for the style classifier.

Runs inside headless Blender:

    blender -b -P render_highpoly_corpus.py -- \
        --input highpoly_src/armadillo.obj --outdir data/highpoly

Renders N yaw x M pitch views of the model, SMOOTH-shaded (the anti-OSRS
look), studio-lit via the Workbench engine, at the same 256px resolution and
on the same mid-gray background as gen_osrs_corpus.py — the two corpora must
differ in *style*, not in framing, resolution, or background, or the
classifier will learn the wrong thing.

Textured models (.glb) render their textures; untextured research scans get a
deterministic per-model material color (hashed from the filename) so the
negative class isn't uniformly white.
"""

import argparse
import colorsys
import hashlib
import math
import os
import sys

import bpy
from mathutils import Vector

# Background handling: Workbench final renders do not honor the viewport
# background settings headlessly, so we render with a TRANSPARENT film and the
# batch driver (gen_highpoly_corpus.py) composites every frame onto the exact
# 0x808080 gray shared with gen_osrs_corpus.py — guaranteed pixel parity.


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    ap = argparse.ArgumentParser(prog="render_highpoly_corpus.py")
    group = ap.add_mutually_exclusive_group(required=True)
    group.add_argument("--input", help="single model file")
    group.add_argument("--manifest",
                       help="text file with one model path per line; the whole "
                            "batch renders in ONE Blender process (startup cost "
                            "is paid once, not per model)")
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--yaws", type=int, default=12)
    ap.add_argument("--pitches", type=float, nargs="*", default=[5.0, 22.0, 40.0],
                    help="camera elevation angles in degrees")
    ap.add_argument("--resolution", type=int, default=256)
    return ap.parse_args(argv)


def import_model(path: str) -> None:
    ext = os.path.splitext(path)[1].lower()
    if ext == ".obj":
        if hasattr(bpy.ops.wm, "obj_import"):
            bpy.ops.wm.obj_import(filepath=path)
        else:
            bpy.ops.import_scene.obj(filepath=path)
    elif ext == ".fbx":
        bpy.ops.import_scene.fbx(filepath=path)
    elif ext in (".glb", ".gltf"):
        bpy.ops.import_scene.gltf(filepath=path)
    elif ext == ".ply":
        bpy.ops.import_mesh.ply(filepath=path)
    elif ext == ".stl":
        bpy.ops.import_mesh.stl(filepath=path)
    else:
        raise SystemExit(f"unsupported format: {ext}")


def stable_color(name: str):
    """Deterministic saturated-ish color from the model filename, so
    untextured scans get variety without run-to-run randomness."""
    h = int(hashlib.sha1(name.encode()).hexdigest()[:8], 16)
    hue = (h % 360) / 360.0
    sat = 0.25 + ((h >> 9) % 50) / 100.0     # 0.25..0.75
    val = 0.55 + ((h >> 17) % 35) / 100.0    # 0.55..0.90
    return colorsys.hsv_to_rgb(hue, sat, val)


def prepare(objname: str):
    """Join meshes, smooth-shade, ensure a material color exists.
    Returns (object, has_texture)."""
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    if not meshes:
        raise SystemExit("no mesh in input")
    bpy.ops.object.select_all(action="DESELECT")
    for m in meshes:
        m.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    # glTF scenes often carry the real transform on parent nodes (the Duck's
    # 0.01 node scale, rigs' armature objects). Bake the WORLD transform into
    # each mesh before the parents are deleted, or the object jumps/rescales.
    bpy.ops.object.parent_clear(type="CLEAR_KEEP_TRANSFORM")
    if len(meshes) > 1:
        bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    for other in list(bpy.context.scene.objects):
        if other is not obj:
            bpy.data.objects.remove(other, do_unlink=True)

    # Smooth shading everywhere — the signature of a "modern" render.
    for poly in obj.data.polygons:
        poly.use_smooth = True

    has_texture = False
    for slot in obj.material_slots:
        mat = slot.material
        if mat and mat.use_nodes and any(
                n.type == "TEX_IMAGE" and n.image for n in mat.node_tree.nodes):
            has_texture = True
            break

    if not has_texture:
        # Give untextured scans a per-model color (Workbench MATERIAL mode
        # reads diffuse_color / the Principled base color).
        rgb = stable_color(objname)
        mat = bpy.data.materials.new("scan_color")
        mat.diffuse_color = (*rgb, 1.0)
        mat.use_nodes = True
        bsdf = next((n for n in mat.node_tree.nodes
                     if n.type == "BSDF_PRINCIPLED"), None)
        if bsdf:
            bsdf.inputs["Base Color"].default_value = (*rgb, 1.0)
        obj.data.materials.clear()
        obj.data.materials.append(mat)
    return obj, has_texture


def setup_render(resolution: int, has_texture: bool):
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.render.resolution_x = resolution
    scene.render.resolution_y = resolution
    scene.render.film_transparent = True   # driver composites onto gray
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    # 'Standard' keeps the background pixel-exact vs the OSRS corpus (Filmic
    # would shift it and hand the classifier a trivial background cue).
    scene.view_settings.view_transform = "Standard"

    sh = scene.display.shading
    sh.light = "STUDIO"
    sh.color_type = "TEXTURE" if has_texture else "MATERIAL"
    sh.show_object_outline = False
    sh.show_shadows = False
    sh.show_cavity = False


def render_views(obj, outdir: str, stem: str, yaws: int, pitches, resolution: int):
    corners = [obj.matrix_world @ Vector(c) for c in obj.bound_box]
    center = sum(corners, Vector()) / 8.0
    radius = max((c - center).length for c in corners) or 1e-6

    cam_data = bpy.data.cameras.new("cam")
    cam_data.lens_unit = "FOV"
    cam_data.angle = math.radians(40.0)
    cam = bpy.data.objects.new("cam", cam_data)
    bpy.context.scene.collection.objects.link(cam)
    bpy.context.scene.camera = cam

    base_dist = radius / math.tan(cam_data.angle / 2.0) * 1.1
    cam_data.clip_start = max(base_dist - radius * 2.0, 1e-4)
    cam_data.clip_end = base_dist + radius * 4.0

    # Deterministic per-model zoom jitter so the corpus varies in framing the
    # way real renders do, without run-to-run randomness.
    h = int(hashlib.sha1(stem.encode()).hexdigest()[8:12], 16)

    shot = 0
    for pi, pitch_deg in enumerate(pitches):
        pitch = math.radians(pitch_deg)
        for yi in range(yaws):
            yaw = 2.0 * math.pi * yi / yaws
            jitter = 1.0 + (((h + shot * 37) % 40) / 100.0)   # 1.00 .. 1.39
            dist = base_dist * jitter
            direction = Vector((
                math.sin(yaw) * math.cos(pitch),
                -math.cos(yaw) * math.cos(pitch),
                math.sin(pitch),
            ))
            cam.location = center + direction * dist
            cam.rotation_euler = direction.to_track_quat("Z", "Y").to_euler()
            out = os.path.join(outdir, f"{stem}_p{pi}_y{yi:02d}.png")
            bpy.context.scene.render.filepath = out
            bpy.ops.render.render(write_still=True)
            shot += 1
    print(f"[highpoly] {stem}: {shot} views -> {outdir}")


def main():
    args = parse_args()
    os.makedirs(args.outdir, exist_ok=True)

    if args.input:
        models = [args.input]
    else:
        with open(args.manifest, encoding="utf-8") as fh:
            models = [ln.strip() for ln in fh if ln.strip()]

    failed = 0
    for path in models:
        # One bad mesh must not sink the whole batch — log and continue.
        try:
            render_one(path, args.outdir, args.yaws, args.pitches, args.resolution)
        except Exception as exc:
            failed += 1
            print(f"[highpoly] FAILED {os.path.basename(path)}: {exc}",
                  file=sys.stderr)
    print(f"[highpoly] batch done: {len(models) - failed}/{len(models)} models")


def render_one(path: str, outdir: str, yaws: int, pitches, resolution: int):
    """Full pipeline for one model, starting from a factory-empty scene so
    manifest batches can't leak state between models."""
    stem = os.path.splitext(os.path.basename(path))[0]
    bpy.ops.wm.read_factory_settings(use_empty=True)
    import_model(os.path.abspath(path))
    obj, has_texture = prepare(stem)
    setup_render(resolution, has_texture)
    render_views(obj, outdir, stem, yaws, pitches, resolution)


if __name__ == "__main__":
    main()
