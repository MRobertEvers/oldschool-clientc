#!/usr/bin/env python3
"""Component 2: headless Blender mutation + render script.

Runs INSIDE Blender's bundled Python (which ships numpy — no pip needed):

    blender -b -P modifier.py -- --input model.fbx --outdir /tmp/trial_0 \
        --decimate 0.05 --grid 0.08 --colors 8

Pipeline, in order (order matters — colors are sampled from the ORIGINAL UVs
before materials are stripped, but written onto the already-simplified mesh):

    import -> join to one mesh -> decimate -> vertex grid quantize + weld
           -> bake palette-quantized vertex colors (strip textures)
           -> force flat shading -> export .glb -> render front/side/iso PNGs

Special parameter values used by the optimizer to produce the BASELINE
(unmodified) renders: --decimate 1.0 --grid 0 --colors 0. With --colors 0 the
materials/textures are kept and the render uses texture mode instead of
vertex-color mode, so the content-preservation anchor shows the true object.

Outputs in --outdir:
    <prefix>_front.png, <prefix>_side.png, <prefix>_iso.png, <prefix>.glb
"""

import argparse
import math
import os
import sys

import bpy
import numpy as np
from mathutils import Vector

# The exact background both corpus classes are rendered on (0x808080), as
# display-referred floats. The style judge only ever saw this gray during
# training, so every render it scores has to land on it too — see setup_render.
CORPUS_BACKGROUND = (0x80 / 255, 0x80 / 255, 0x80 / 255)

# ---------------------------------------------------------------------------
# CLI — Blender passes everything after "--" through untouched in sys.argv.
# ---------------------------------------------------------------------------

def parse_args() -> argparse.Namespace:
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    ap = argparse.ArgumentParser(prog="modifier.py")
    ap.add_argument("--input", required=True,
                    help=".obj, .fbx, .glb or .gltf to stylize")
    ap.add_argument("--outdir", required=True, help="folder for renders + .glb")
    ap.add_argument("--decimate", type=float, default=0.1,
                    help="Decimate modifier ratio, (0,1]; 1.0 = no reduction")
    ap.add_argument("--grid", type=float, default=0.05,
                    help="vertex quantization grid spacing in object units; 0 = off")
    ap.add_argument("--colors", type=int, default=8,
                    help="palette size for baked vertex colors; 0 = keep textures")
    ap.add_argument("--resolution", type=int, default=512, help="square render size")
    ap.add_argument("--prefix", default="view", help="render filename prefix")
    ap.add_argument("--bg", type=float, nargs=3, default=CORPUS_BACKGROUND,
                    help="solid background color as 3 DISPLAY-referred floats "
                         "0..1 (i.e. what you'd read off a color picker). The "
                         "default is the corpus gray and should be left alone "
                         "unless you retrain on a different background.")
    return ap.parse_args(argv)


# ---------------------------------------------------------------------------
# Scene setup / import
# ---------------------------------------------------------------------------

def reset_scene() -> None:
    """Start from a truly empty scene — no default cube, camera, or light."""
    bpy.ops.wm.read_factory_settings(use_empty=True)


def import_model(path: str) -> None:
    """Import .obj, .fbx, or .glb/.gltf, handling the Blender 4.x OBJ operator
    rename. glTF matters here beyond convenience: it is the only one of these
    the corpus fetcher downloads with real textures, and the color-quantization
    stage has nothing to quantize without them."""
    ext = os.path.splitext(path)[1].lower()
    if ext == ".fbx":
        bpy.ops.import_scene.fbx(filepath=path)
    elif ext == ".obj":
        if hasattr(bpy.ops.wm, "obj_import"):        # Blender >= 3.3 (new C++ importer)
            bpy.ops.wm.obj_import(filepath=path)
        else:                                         # legacy Python importer
            bpy.ops.import_scene.obj(filepath=path)
    elif ext in (".glb", ".gltf"):
        bpy.ops.import_scene.gltf(filepath=path)
    else:
        raise SystemExit(
            f"Unsupported input format: {ext} (use .obj, .fbx, .glb, or .gltf)")


def unify_to_single_mesh() -> bpy.types.Object:
    """Join every imported mesh into one object and apply its transforms, so
    decimation/quantization/framing all operate in one clean local space."""
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    if not meshes:
        raise SystemExit("No mesh objects found in the imported file")

    bpy.ops.object.select_all(action="DESELECT")
    for obj in meshes:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    # glTF scenes routinely carry the real transform on parent nodes (the
    # Khronos Duck's 0.01 node scale, any rigged asset's armature). Bake the
    # world transform into each mesh before the parents are deleted below, or
    # the object silently jumps or rescales.
    bpy.ops.object.parent_clear(type="CLEAR_KEEP_TRANSFORM")
    if len(meshes) > 1:
        bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active

    # Bake location/rotation/scale into vertex coordinates: the quantization
    # grid must be uniform in world space, not warped by a leftover scale.
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

    # Non-mesh imports (empties, armatures, embedded lights/cameras) would
    # pollute the render and the .glb export — remove them.
    for other in list(bpy.context.scene.objects):
        if other is not obj:
            bpy.data.objects.remove(other, do_unlink=True)
    return obj


# ---------------------------------------------------------------------------
# Geometry mutations
# ---------------------------------------------------------------------------

def apply_decimate(obj: bpy.types.Object, ratio: float) -> None:
    """Collapse-decimate to `ratio` of the original face count. Collapse mode
    triangulates as a side effect — which is exactly right for OSRS, whose
    models are pure triangle soup."""
    if ratio >= 1.0:
        return
    mod = obj.modifiers.new(name="osrs_decimate", type="DECIMATE")
    mod.decimate_type = "COLLAPSE"
    mod.ratio = max(ratio, 1e-4)  # ratio 0 would delete the whole mesh
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.modifier_apply(modifier=mod.name)


def quantize_vertices(obj: bpy.types.Object, grid: float) -> None:
    """Snap every vertex to a `grid`-spaced lattice, then weld vertices that
    landed on the same point. The welding is deliberate: collapsed edges and
    slightly degenerate faces are a large part of the authentic "janky" retro
    silhouette, and RuneScape's own low-unit-precision coordinates do the same
    thing. Uses numpy foreach_get/set — fast even on multi-million-vert input."""
    if grid <= 0.0:
        return
    mesh = obj.data
    n = len(mesh.vertices)
    coords = np.empty(n * 3, dtype=np.float32)
    mesh.vertices.foreach_get("co", coords)
    coords = np.round(coords / grid) * grid          # the actual quantization
    mesh.vertices.foreach_set("co", coords)
    mesh.update()

    # Weld exactly-coincident vertices (they snapped to the same lattice cell).
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.remove_doubles(threshold=grid * 1e-3)  # << grid, so only true overlaps merge
    bpy.ops.mesh.delete_loose()                          # drop stranded verts/edges
    bpy.ops.object.mode_set(mode="OBJECT")


# ---------------------------------------------------------------------------
# Color quantization: texture/material -> N-color vertex-color palette
# ---------------------------------------------------------------------------

def _image_as_array(image: bpy.types.Image):
    """Read a Blender image's pixels into an (H, W, C) float32 numpy array.
    foreach_get is orders of magnitude faster than iterating image.pixels."""
    w, h, c = image.size[0], image.size[1], image.channels
    if w == 0 or h == 0:
        return None
    buf = np.empty(w * h * c, dtype=np.float32)
    image.pixels.foreach_get(buf)
    return buf.reshape(h, w, c)


def _material_samplers(obj: bpy.types.Object):
    """For each material slot, return either ('image', np.ndarray) if we can
    find an Image Texture feeding Base Color, or ('flat', rgb) fallback taken
    from the Principled base color / viewport color. Index -1 (no material)
    maps to neutral gray."""
    samplers = []
    for slot in obj.material_slots:
        mat = slot.material
        entry = ("flat", np.array([0.6, 0.6, 0.6], dtype=np.float32))
        if mat is not None:
            if mat.use_nodes:
                # Prefer any image plugged into the node tree (usually albedo).
                image = None
                for node in mat.node_tree.nodes:
                    if node.type == "TEX_IMAGE" and node.image is not None:
                        image = node.image
                        break
                if image is not None:
                    arr = _image_as_array(image)
                    if arr is not None:
                        entry = ("image", arr)
                if entry[0] == "flat":
                    bsdf = next((n for n in mat.node_tree.nodes
                                 if n.type == "BSDF_PRINCIPLED"), None)
                    if bsdf is not None:
                        rgba = bsdf.inputs["Base Color"].default_value
                        entry = ("flat", np.array(rgba[:3], dtype=np.float32))
            else:
                entry = ("flat", np.array(mat.diffuse_color[:3], dtype=np.float32))
        samplers.append(entry)
    return samplers


def _sample_loop_colors(obj: bpy.types.Object) -> np.ndarray:
    """Return an (n_loops, 3) array of source colors, one per face corner,
    by sampling each material's texture at the loop's UV (nearest-neighbor,
    with wrap-around) or using the material's flat color."""
    mesh = obj.data
    n_loops = len(mesh.loops)
    colors = np.full((n_loops, 3), 0.6, dtype=np.float32)
    samplers = _material_samplers(obj)

    uv_layer = mesh.uv_layers.active
    uvs = None
    if uv_layer is not None:
        uvs = np.empty(n_loops * 2, dtype=np.float32)
        uv_layer.data.foreach_get("uv", uvs)
        uvs = uvs.reshape(n_loops, 2)

    for poly in mesh.polygons:
        kind, payload = (samplers[poly.material_index]
                         if 0 <= poly.material_index < len(samplers)
                         else ("flat", np.array([0.6, 0.6, 0.6], dtype=np.float32)))
        ls, le = poly.loop_start, poly.loop_start + poly.loop_total
        if kind == "image" and uvs is not None:
            tex = payload
            h, w = tex.shape[0], tex.shape[1]
            uv = uvs[ls:le]
            # Wrap UVs into [0,1) then map to texel indices (nearest sample).
            x = np.clip((np.mod(uv[:, 0], 1.0) * w).astype(np.int64), 0, w - 1)
            y = np.clip((np.mod(uv[:, 1], 1.0) * h).astype(np.int64), 0, h - 1)
            colors[ls:le] = tex[y, x, :3]
        else:
            colors[ls:le] = payload
    return colors


def _kmeans_palette(colors: np.ndarray, k: int, iters: int = 24,
                    seed: int = 1337) -> np.ndarray:
    """Tiny numpy k-means returning each sample snapped to its centroid.
    Implemented by hand because Blender's Python has numpy but not sklearn.
    Subsamples for centroid fitting so huge meshes stay fast; the final
    assignment pass covers every loop."""
    rng = np.random.default_rng(seed)
    k = min(k, len(np.unique(colors.round(3), axis=0)))  # can't have more clusters than colors
    k = max(k, 1)

    fit = colors if len(colors) <= 50_000 else colors[rng.choice(len(colors), 50_000, replace=False)]
    centroids = fit[rng.choice(len(fit), k, replace=False)].copy()

    for _ in range(iters):
        # Assign each fitting sample to its nearest centroid (squared L2 in RGB).
        d = ((fit[:, None, :] - centroids[None, :, :]) ** 2).sum(axis=2)
        labels = d.argmin(axis=1)
        for ci in range(k):
            members = fit[labels == ci]
            if len(members):
                centroids[ci] = members.mean(axis=0)
            else:
                # Re-seed empty clusters onto a random sample so k stays honest.
                centroids[ci] = fit[rng.integers(len(fit))]

    # Final full-resolution snap of every loop color to the learned palette.
    d = ((colors[:, None, :] - centroids[None, :, :]) ** 2).sum(axis=2)
    return centroids[d.argmin(axis=1)]


def bake_vertex_colors(obj: bpy.types.Object, n_colors: int) -> None:
    """Sample source colors per face corner, quantize to an N-color palette,
    write them into a FLOAT_COLOR corner attribute, and strip all materials
    (OSRS assets have no textures — only per-face colors)."""
    if n_colors <= 0:
        return
    mesh = obj.data

    loop_colors = _sample_loop_colors(obj)
    loop_colors = _kmeans_palette(loop_colors, n_colors)

    # Remove stale color attributes, then create ours as the active one so
    # both the Workbench renderer and the glTF exporter pick it up.
    for attr in list(mesh.color_attributes):
        mesh.color_attributes.remove(attr)
    attr = mesh.color_attributes.new(name="Col", type="FLOAT_COLOR", domain="CORNER")
    rgba = np.concatenate(
        [loop_colors, np.ones((len(loop_colors), 1), dtype=np.float32)], axis=1)
    attr.data.foreach_set("color", rgba.ravel())
    mesh.color_attributes.active_color = attr

    mesh.materials.clear()  # textures gone — the palette is now the material
    mesh.update()


def force_flat_shading(obj: bpy.types.Object) -> None:
    """Every face flat-shaded: kill smooth flags and any custom split normals
    / auto-smooth data the importer may have brought along."""
    mesh = obj.data
    smooth = np.zeros(len(mesh.polygons), dtype=bool)
    mesh.polygons.foreach_set("use_smooth", smooth)
    if hasattr(mesh, "use_auto_smooth"):        # removed in Blender 4.1+
        mesh.use_auto_smooth = False
    try:
        bpy.context.view_layer.objects.active = obj
        bpy.ops.mesh.customdata_custom_splitnormals_clear()
    except RuntimeError:
        pass  # no custom normal layer present — nothing to clear
    mesh.update()


# ---------------------------------------------------------------------------
# Rendering (Workbench engine: deterministic, CPU-headless-safe, faceted look)
# ---------------------------------------------------------------------------

def setup_render(resolution: int, use_vertex_color: bool) -> None:
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.render.resolution_x = resolution
    scene.render.resolution_y = resolution
    scene.render.image_settings.file_format = "PNG"
    # Transparent film + an explicit composite (see composite_background), which
    # is the same contract render_highpoly_corpus.py uses. Workbench's viewport
    # background settings are simply ignored by headless final renders —
    # setting shading.background_color to seven different values produces seven
    # identical frames — so painting the gray in Blender is not an option.
    scene.render.film_transparent = True
    scene.render.image_settings.color_mode = "RGBA"
    # 'Standard', matching gen_osrs_corpus/render_highpoly_corpus. Blender 4's
    # default AgX view transform tone-maps the whole frame, which would shift
    # the object colors away from the corpus the judge was trained on.
    scene.view_settings.view_transform = "Standard"

    shading = scene.display.shading
    shading.light = "STUDIO"       # built-in studio lights — no lamps needed
    shading.show_object_outline = False
    shading.show_shadows = False
    shading.show_cavity = False
    # Stylized trials render the baked palette; baseline renders the textures.
    shading.color_type = "VERTEX" if use_vertex_color else "TEXTURE"


def composite_background(path: str, bg) -> None:
    """Flatten one transparent-film render onto `bg`, in place.

    Blender's image `pixels` for an 8-bit PNG are display-referred (a 0x80 gray
    reads back as 0.50196, not its 0.21586 linear equivalent) and save_render
    writes them straight back out, so the whole operation stays in display
    space and 0x80 in means exactly 0x80 out.

    Compositing in display space is also what we *want* rather than merely what
    is convenient: gen_highpoly_corpus.py flattens the entire Class 2 corpus
    with PIL's paste(mask=alpha), which does exactly this. Doing the more
    physically-correct linear blend here would put every scored render slightly
    off the distribution the judge was trained on, which is the whole bug this
    function exists to close.
    """
    img = bpy.data.images.load(path)
    try:
        px = np.asarray(img.pixels[:], dtype=np.float32).reshape(-1, 4)
        alpha = px[:, 3:4]
        px[:, :3] = px[:, :3] * alpha + np.asarray(bg, dtype=np.float32) * (1.0 - alpha)
        px[:, 3] = 1.0
        img.pixels = px.ravel().tolist()
        img.file_format = "PNG"
        # Write 3-channel RGB, matching the corpus tiles exactly. The alpha is
        # uniformly 1.0 by now so nothing is lost, but leaving a channel behind
        # invites a future consumer to composite against it a second time.
        settings = bpy.context.scene.render.image_settings
        settings.color_mode = "RGB"
        try:
            img.save_render(filepath=path, scene=bpy.context.scene)
        finally:
            settings.color_mode = "RGBA"   # the next view still renders a film
    finally:
        # Renders run in a loop; leaking a datablock per view would grow the
        # blend file for no reason.
        bpy.data.images.remove(img)


def _bounding_sphere(obj: bpy.types.Object):
    """World-space center and radius from the object's bounding box."""
    corners = [obj.matrix_world @ Vector(c) for c in obj.bound_box]
    center = sum(corners, Vector()) / 8.0
    radius = max((c - center).length for c in corners)
    return center, max(radius, 1e-6)


def render_views(obj: bpy.types.Object, outdir: str, prefix: str, bg) -> None:
    """Render the three canonical views. Camera distance is derived from the
    bounding sphere and the camera FOV so any model, any scale, fills the
    frame consistently — critical, because both scorers compare renders and
    must not be confounded by framing differences."""
    center, radius = _bounding_sphere(obj)

    cam_data = bpy.data.cameras.new("osrs_cam")
    cam_data.lens_unit = "FOV"
    cam_data.angle = math.radians(40.0)
    cam = bpy.data.objects.new("osrs_cam", cam_data)
    bpy.context.scene.collection.objects.link(cam)
    bpy.context.scene.camera = cam

    # Fit the bounding sphere inside the FOV cone, with a small margin.
    distance = radius / math.tan(cam_data.angle / 2.0) * 1.15
    cam_data.clip_start = max(distance - radius * 2.0, 0.01)
    cam_data.clip_end = distance + radius * 4.0

    views = {
        "front": Vector((0.0, -1.0, 0.0)),
        "side": Vector((1.0, 0.0, 0.0)),
        "iso": Vector((1.0, -1.0, 1.0)).normalized(),  # classic 45°/35° isometric
    }
    for name, direction in views.items():
        cam.location = center + direction * distance
        # Aim the camera's -Z axis back at the model center, Y up.
        cam.rotation_euler = direction.to_track_quat("Z", "Y").to_euler()
        path = os.path.join(outdir, f"{prefix}_{name}.png")
        bpy.context.scene.render.filepath = path
        bpy.ops.render.render(write_still=True)
        composite_background(path, bg)


def export_glb(obj: bpy.types.Object, path: str) -> None:
    """Export the stylized mesh. glTF is used (not OBJ) because it reliably
    round-trips vertex colors across Blender versions."""
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    args = parse_args()
    os.makedirs(args.outdir, exist_ok=True)

    reset_scene()
    import_model(os.path.abspath(args.input))
    obj = unify_to_single_mesh()
    print(f"[modifier] imported: {len(obj.data.polygons)} faces, "
          f"{len(obj.data.vertices)} verts")

    apply_decimate(obj, args.decimate)
    quantize_vertices(obj, args.grid)
    bake_vertex_colors(obj, args.colors)   # also strips materials when active
    force_flat_shading(obj)
    print(f"[modifier] stylized: {len(obj.data.polygons)} faces, "
          f"{len(obj.data.vertices)} verts")

    export_glb(obj, os.path.join(args.outdir, f"{args.prefix}.glb"))

    setup_render(args.resolution, use_vertex_color=args.colors > 0)
    render_views(obj, args.outdir, args.prefix, args.bg)
    print(f"[modifier] done -> {args.outdir}")


if __name__ == "__main__":
    main()
