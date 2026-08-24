# ToriDraw raster kernels

Status: implemented design and public contract.

## Purpose

Raster kernels are the per-call extension point between model-face preparation
and triangle rasterization. A caller can choose a built-in normal or smooth SD
variant and its branching or scanline implementation at runtime, replace
selected face classes, or provide a complete renderer without changing a scene
or model.

The boundary is intentionally above triangle setup and span/pixel loops:

```text
project -> sort or back-face cull -> prepare face -> one kernel callback
                                               -> direct triangle/span/pixel code
```

ToriDraw owns model interpretation. It rejects hidden or unsupported faces,
resolves and validates texture/mapping inputs, normalizes shades and opacity,
and prepares clipping state before the callback. A kernel therefore receives a
semantic face instead of having to decode `face_infos` or reinterpret P/M/N
records.

The interface covers the software face loops for `TORIDRAWMK_MODEL` and
`TORIDRAWMK_MODEL_HD`. Ground meshes use their existing ground/GPU/packet paths
and are not raster-kernel inputs.

## Typed public interface

There is no untyped kernel base. Stock/SD and HD have separate callback, face,
texture, vtable, kernel, and fallback types in
[`toridraw_raster_kernel.h`](../3rd/toridraw/toridraw_raster_kernel.h). They share
only `ToriDraw_RasterTarget`.

SD has four terminal face classes:

| `ToriDraw_RasterFaceClassSD` | Meaning |
|---|---|
| `TORIDRAW_RASTER_FACE_SD_GOURAUD` | Untextured, three prepared shades |
| `TORIDRAW_RASTER_FACE_SD_FLAT` | Untextured, one shade repeated three times |
| `TORIDRAW_RASTER_FACE_SD_TEXTURED` | Textured, three prepared shades |
| `TORIDRAW_RASTER_FACE_SD_TEXTURED_FLAT` | Textured, one shade repeated three times |

HD has six terminal face classes:

| `ToriDraw_RasterFaceClassHD` | Meaning |
|---|---|
| `TORIDRAW_RASTER_FACE_HD_GOURAUD` | Solid Gouraud fallback or untextured face |
| `TORIDRAW_RASTER_FACE_HD_FLAT` | Solid flat fallback or untextured face |
| `TORIDRAW_RASTER_FACE_HD_PLANE` | P/M/N vertex-frame projection |
| `TORIDRAW_RASTER_FACE_HD_CYLINDER` | Cylinder mapping |
| `TORIDRAW_RASTER_FACE_HD_CUBE` | Cube mapping |
| `TORIDRAW_RASTER_FACE_HD_SPHERE` | Sphere mapping |

Texture gate, face alpha, modulation, clamping, affine mapping, near clipping,
and depth testing are inputs to those algorithms. They are not additional
vtable slots. SD smooth Gouraud shading is instead an implementation choice of
the selected kernel: the four-slot SD shape is identical for normal and smooth
kernels.

The callback types are deliberately distinct:

```c
typedef void (*ToriDraw_RasterKernelSDFaceFn)(
    void *user_data,
    const struct ToriDraw_RasterTarget *target,
    const struct ToriDraw_RasterFaceSD *face);

typedef void (*ToriDraw_RasterKernelHDFaceFn)(
    void *user_data,
    const struct ToriDraw_RasterTarget *target,
    const struct ToriDraw_RasterFaceHD *face);
```

The public vtables and objects are:

```c
struct ToriDraw_RasterKernelSDVTable {
    ToriDraw_RasterKernelSDFaceFn draw_gouraud;
    ToriDraw_RasterKernelSDFaceFn draw_flat;
    ToriDraw_RasterKernelSDFaceFn draw_textured;
    ToriDraw_RasterKernelSDFaceFn draw_textured_flat;
};

struct ToriDraw_RasterKernelHDVTable {
    ToriDraw_RasterKernelHDFaceFn draw_gouraud;
    ToriDraw_RasterKernelHDFaceFn draw_flat;
    ToriDraw_RasterKernelHDFaceFn draw_plane;
    ToriDraw_RasterKernelHDFaceFn draw_cylinder;
    ToriDraw_RasterKernelHDFaceFn draw_cube;
    ToriDraw_RasterKernelHDFaceFn draw_sphere;
};

struct ToriDraw_RasterKernelSD {
    const struct ToriDraw_RasterKernelSDVTable *vtable;
    void *user_data;
    const struct ToriDraw_RasterKernelSD *fallback;
};

struct ToriDraw_RasterKernelHD {
    const struct ToriDraw_RasterKernelHDVTable *vtable;
    void *user_data;
    const struct ToriDraw_RasterKernelHD *fallback;
};
```

The kernel, vtable, fallback chain, and `user_data` are borrowed for the render
call. They must remain alive and immutable until it returns. A vtable should
normally be `static const`; a kernel and its state may be stack objects when the
render call is synchronous.

## Prepared descriptors

### Shared target

`ToriDraw_RasterTarget` is stable for one model raster call. It provides:

- the writable pixel buffer and optional z-buffer, rebased to the clip origin;
- local width, height, stride, clip origin, and projection centre;
- near-plane, camera projection, model-depth, parallel/affine/depth, and
  near-clip-availability values;
- projected screen and orthographic vertex arrays; and
- posed and bind-pose model-space vertex arrays.

Screen coordinates in the arrays are relative to `projection_center_x` and
`projection_center_y`. `pixel_buffer` and `zbuffer` already point at the local
clip origin. All arrays are borrowed scene or model storage and remain valid
only for the call.

`internal` is reserved for built-in ToriDraw kernels. Application callbacks
must use the public fields and must not inspect, replace, or retain it.

### Faces

`ToriDraw_RasterFaceSD` and `ToriDraw_RasterFaceHD` are separate normalized
descriptors. Both contain the typed class, original face index, three model
vertex indices, three shades, effective opacity from 0 through 255, and a
`near_clipped` flag.

Flat faces repeat `shade[0]` in all three entries. The descriptor is reused for
later faces, so a callback must not retain its address.

SD embeds `ToriDraw_RasterTextureSD`. For textured classes it contains the
resolved texture id, texels, dimensions and gate, the raw stock render type,
the selected P/M/N vertex frame, and whether that frame is a face-frame
fallback. Stock textured faces retain their historical behavior of ignoring
authored face alpha, so their prepared opacity is 255.

HD embeds `ToriDraw_RasterTextureHD`. In addition to resolved texture data it
contains clamp flags, raw render type, either a vertex frame or a validated
`ToriDraw_TexMapping`, mapping-fallback state, and modulation tint/neutral
values. A missing HD material becomes a visible solid Gouraud/flat face. A
mapped type without mapping data becomes a plane face with
`frame_fallback == true`. These policy decisions happen before dispatch.

The texture member is meaningful only for a textured SD class or one of the
four HD texture-projection classes.

SD built-ins rebuild supported near-clipped triangles from the prepared
orthographic arrays. The HD descriptors expose the same availability and face
flag, but the current built-in HD projection families retain their existing
non-rebuilding near-plane behavior; the interface does not claim to repair
that separate limitation.

## Per-call selection

The explicit SD entry points are:

- `ToriDraw_RenderModelWithRasterKernel`;
- `ToriDraw_RenderModel3RasterWithRasterKernel`; and
- `ToriDraw_RenderZBufferedWithRasterKernel`.

The kernel-explicit phase-three and Z-buffered APIs do not take a `smooth`
boolean, and `ToriDraw_RasterTarget` has no smooth-shading field. Selecting a
normal root, a smooth root, or an application kernel completely specifies that
choice. If an override needs distinct normal and smooth behavior, expose two
kernel objects or encode that distinction in its `user_data`; callbacks cannot
infer it from the target.

The explicit HD entry points are:

- `ToriDraw_RenderHDWithRasterKernel`; and
- `ToriDraw_RenderHDZBufferedWithRasterKernel`.

The kernel pointer is an argument to the call. It is not stored on
`ToriDraw_Scene`, `ToriDraw_Model`, or `ToriDraw_ModelHD`, so two consecutive
calls can use different variants without mutation or synchronization of model
state.

Normal and Z-buffered calls use the same typed kernel. Depth is reported by
`target->depth_test` and `target->zbuffer`; built-in callbacks select their
depth-tested terminal family from that modifier. The Z-buffered convenience
flows also omit face sorting and perform back-face culling in model order. The
ordinary SD flow may enable depth for a model through its existing model/scene
z-buffer policy without changing kernel type.

Projection, sorting, picking, model priority, and visibility limits remain
outside the interface. A kernel cannot recover a face discarded before face
preparation.

## Built-in roots and compatibility wrappers

ToriDraw exposes six immutable, process-lifetime terminal roots. SD provides
normal and smooth implementations of both raster families; HD provides its
branching and scanline implementations:

```c
const struct ToriDraw_RasterKernelSD *ToriDraw_RasterKernelSDGetBranching(void);
const struct ToriDraw_RasterKernelSD *ToriDraw_RasterKernelSDGetScanline(void);
const struct ToriDraw_RasterKernelSD *ToriDraw_RasterKernelSDGetSmoothBranching(void);
const struct ToriDraw_RasterKernelSD *ToriDraw_RasterKernelSDGetSmoothScanline(void);
const struct ToriDraw_RasterKernelHD *ToriDraw_RasterKernelHDGetBranching(void);
const struct ToriDraw_RasterKernelHD *ToriDraw_RasterKernelHDGetScanline(void);
```

The legacy render functions choose a matching static root once per call from
the process-wide `ToriDraw_RasterGetScanline()` selector. The legacy `smooth`
argument on `ToriDraw_RenderModel3Raster` and `ToriDraw_RenderZBuffered` exists
only to select the corresponding normal or smooth static SD root; it is not
forwarded as raster state. `ToriDraw_RenderModel` and model-sprite helpers use a
normal SD root. `ToriDraw_RenderHD` and `ToriDraw_RenderHDZBuffered` choose one
of the two HD roots.

`ToriDraw_RasterSetScanline(bool)` changes that compatibility choice, and
`ToriDraw_Init()` recognizes `TORIDRAW_RASTER_SCANLINE=1`. Explicit-kernel calls
are unaffected. New code that needs per-object or per-call selection should
pass one of the root pointers directly instead of changing the global selector.

## Sparse overrides and typed fallback

A null vtable slot means “continue through this kernel's typed `fallback`
chain.” Resolution walks the chain once before the face loop and records both
the first function and the `user_data` belonging to the node that supplied it.
Fallback is not traversed per face.

An explicit chain must supply every slot. ToriDraw does not append a hidden
default, so a sparse application kernel should end at an explicit complete root
of the same type. A null kernel, null vtable, cycle, or unresolved slot makes
the raster stage fail. To suppress a face class deliberately, install a no-op
callback; do not leave its slot null.

An explicit callback replaces its slot. Fallback does not run after it. An
instrumenting callback that also wants the built-in result can call the chosen
complete root's corresponding vtable function itself.

### Sparse SD override

This example counts flat faces and then delegates them to the selected built-in
variant. All other classes resolve directly from `base`:

```c
struct FlatCounter {
    int count;
    const struct ToriDraw_RasterKernelSD *base;
};

static void
counted_flat(void *opaque,
             const struct ToriDraw_RasterTarget *target,
             const struct ToriDraw_RasterFaceSD *face)
{
    struct FlatCounter *counter = opaque;
    counter->count++;
    counter->base->vtable->draw_flat(counter->base->user_data, target, face);
}

static const struct ToriDraw_RasterKernelSDVTable counter_vtable = {
    .draw_flat = counted_flat,
};

/* In the calling function: */
const struct ToriDraw_RasterKernelSD *base;
if (smooth) {
    base = use_scanline
        ? ToriDraw_RasterKernelSDGetSmoothScanline()
        : ToriDraw_RasterKernelSDGetSmoothBranching();
} else {
    base = use_scanline
        ? ToriDraw_RasterKernelSDGetScanline()
        : ToriDraw_RasterKernelSDGetBranching();
}
struct FlatCounter counter = { .base = base };
struct ToriDraw_RasterKernelSD kernel = {
    .vtable = &counter_vtable,
    .user_data = &counter,
    .fallback = base,
};

int result = ToriDraw_RenderModelWithRasterKernel(
    model, scene, &position, &viewport, &camera, pixels, &kernel);
```

An HD override uses the same pattern with `ToriDraw_RasterKernelHD`, its
six-slot vtable, and an HD root. For example, setting only `draw_cube` replaces
cube-mapped faces while the typed fallback supplies solid, plane, cylinder, and
sphere slots.

### Complete wireframe kernel

A complete SD wireframe variant can point all four slots at one callback. This
minimal example skips near-plane intersections; a production version should
clip them using the orthographic arrays when `near_clip_available` is true.
`app_draw_line` is application code, not a ToriDraw API.

```c
static void
draw_wireframe(void *opaque,
               const struct ToriDraw_RasterTarget *target,
               const struct ToriDraw_RasterFaceSD *face)
{
    if (face->near_clipped)
        return;

    int x[3], y[3];
    for (int i = 0; i < 3; i++) {
        int vertex = face->vertex[i];
        x[i] = target->screen_vertices_x[vertex] + target->projection_center_x;
        y[i] = target->screen_vertices_y[vertex] + target->projection_center_y;
    }

    app_draw_line(opaque, target, x[0], y[0], x[1], y[1]);
    app_draw_line(opaque, target, x[1], y[1], x[2], y[2]);
    app_draw_line(opaque, target, x[2], y[2], x[0], y[0]);
}

static const struct ToriDraw_RasterKernelSDVTable wire_vtable = {
    .draw_gouraud = draw_wireframe,
    .draw_flat = draw_wireframe,
    .draw_textured = draw_wireframe,
    .draw_textured_flat = draw_wireframe,
};

struct ToriDraw_RasterKernelSD wire = {
    .vtable = &wire_vtable,
    .user_data = &wire_state,
};
```

This kernel is complete and needs no fallback. A depth-aware wireframe must
honor `target->depth_test` and update/test the supplied z-buffer. HD wireframe
support needs a separate HD callback and all six HD slots because the face
descriptor type is different.

## Scratch ownership and reentrancy

`ToriDraw_SceneNew` allocates one reusable projection/sort scratch set sized by
the selected `ToriDraw_ScratchBufferSize`: six projected/orthographic vertex
arrays, face order, and the selected full or small sorter buffers. Rendering
uses stack-local preparation contexts and descriptors that point into this
scene-owned storage; it does not allocate a per-call context or swap scratch
fields.

The same scene is consequently non-reentrant. Do not recursively render from a
kernel callback with that scene, and do not render it concurrently from
multiple threads. There is deliberately no reentry machinery: the API does not
detect or serialize either case, and they would overwrite the active projection
and sort scratch. Use a separate scene, with its own startup allocation, for an
independent nested render. The normal use case remains one render thread per
scene.

The optional model z-buffer is also scene-owned and reused. It can be sized
before rendering with `ToriDraw_SceneZBufferResize`; otherwise a depth-enabled
entry point may grow it on first use. It never grows per face.

Kernel objects themselves contain no scene scratch. Immutable kernels can be
shared, provided any application `user_data` follows the caller's threading
rules and remains valid for every active call.

## `TORIDRAW_PIXEL16`

The typed public interface and distinct branching/scanline root objects remain
available in a 16-bit-pixel build, with these behavior differences:

- SD rendering works, but texture rasterization is compiled out. Faces that
  carry textures are classified as normalized Gouraud or flat SD faces, so the
  textured SD slots are complete but unreachable from production
  classification.
- The depth-tested 32-bit family is absent.
  `ToriDraw_RenderZBufferedWithRasterKernel` is unsupported: it asserts in an
  assertion-enabled build and returns `TORIDRAW_CULL_ERROR` otherwise. Model
  depth flags are inert in the ordinary flow.
- HD roots remain valid complete typed objects, but all HD and HD-Z render
  entry points report `TORIDRAW_CULL_ERROR`, clear supplied HD statistics, and
  do not invoke application callbacks.

An SD override intended for both pixel formats should implement meaningful
Gouraud and flat behavior and must not assume a texture descriptor is active
for those classes.

## Performance contract

The implementation keeps dispatch outside inner raster loops:

1. Validate the typed fallback chain and resolve four or six slots once per
   raster call.
2. Prepare one stack descriptor for each drawable face.
3. Make one indirect callback through the resolved `{function, user_data}`
   slot.
4. Let the selected callback enter direct triangle, span, and pixel code.

There is no per-face fallback walk, scratch-field exchange, or synchronization
branch; the selected kernel is passed down directly. Kernel resolution and
dispatch allocate no heap storage. Separate lazy texture-state or z-buffer
provisioning may allocate before the face loop and can be paid at startup when
required. The compatibility inputs are read once when their wrapper chooses a
root. In particular, normal versus smooth SD Gouraud is encoded by that root,
not carried through the explicit raster core or tested per face. Runtime
choices inside a built-in callback are semantic modifiers such as depth,
affine mapping, gate, alpha, and modulation.

## Acceptance criteria

The kernel boundary is correct when tests establish all of the following:

- every drawable SD class reaches exactly one of four typed slots;
- every drawable HD class reaches exactly one of six typed slots;
- flat shades, opacity, vertex indices, texture gates, frames/mappings, clip
  state, and target arrays are normalized as documented;
- all four normal/smooth SD roots and both HD roots preserve their corresponding
  legacy output;
- sparse chains keep the `user_data` of the node supplying each slot, while
  cycles and incomplete chains fail before the face loop;
- the same SD or HD override works in normal and depth-enabled entry points and
  observes depth through the target rather than a different vtable shape;
- changing the kernel argument affects only that call and writes no scene or
  model kernel state; and
- Pixel16 follows the reduced behavior above.

The relevant implementation is in
[`toridraw_raster_kernel.c`](../3rd/toridraw/toridraw_raster_kernel.c),
[`toridraw_raster.u.c`](../3rd/toridraw/toridraw_raster.u.c), and
[`toridraw_render_hd.u.c`](../3rd/toridraw/toridraw_render_hd.u.c).
