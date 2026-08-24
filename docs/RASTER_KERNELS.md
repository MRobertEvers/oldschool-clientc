# ToriDraw raster kernels

Status: implemented design and public contract.

## Purpose

Raster kernels are the per-call extension point between model-face preparation
and triangle rasterization. A caller can choose a built-in painter or depth
variant at runtime, select normal or smooth SD painter shading and its branching
or scanline implementation, override face classes with a complete application
kernel, or provide a complete renderer without changing a scene or model.

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
texture, vtable, and kernel types in
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

Texture gate, face alpha, modulation, clamping, affine mapping, and near
clipping are inputs to those algorithms. They are not additional vtable slots.
Depth testing is a pass mode selected by the kernel flags and implemented by a
specialized vtable, not another face-class slot. SD smooth Gouraud shading is
also an implementation choice of the selected kernel: the four-slot SD shape
is identical for normal and smooth kernels.

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
enum ToriDraw_RasterKernelFlags {
    TORIDRAW_RASTER_KERNEL_FLAG_NONE = 0,
    TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING = 1u << 0,
    TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER = 1u << 1,
};

struct ToriDraw_RasterKernelSDVTable {
    ToriDraw_RasterKernelSDFaceFn
        draw[TORIDRAW_RASTER_FACE_SD_CLASS_COUNT];
};

struct ToriDraw_RasterKernelHDVTable {
    ToriDraw_RasterKernelHDFaceFn
        draw[TORIDRAW_RASTER_FACE_HD_CLASS_COUNT];
};

struct ToriDraw_RasterKernelSD {
    const struct ToriDraw_RasterKernelSDVTable *vtable;
    void *user_data;
    uint32_t flags;
};

struct ToriDraw_RasterKernelHD {
    const struct ToriDraw_RasterKernelHDVTable *vtable;
    void *user_data;
    uint32_t flags;
};
```

Each face-class value is the direct index of its callback in `draw`; the
`*_CLASS_COUNT` member sizes the complete table and is not a drawable class.
The kernel, vtable, flags, and `user_data` are borrowed for the render call.
They must remain alive and immutable until it returns. A vtable should normally
be `static const`; a kernel and its state may be stack objects when the render
call is synchronous. Every vtable slot is mandatory and must be non-null.
Supplying an incomplete kernel or unknown flag bit violates the API contract.

## Pass orchestration flags

The two flags are independent axes. Their four combinations completely define
face traversal and depth setup for that kernel call:

| `flags` | Face traversal | Pixel visibility |
|---|---|---|
| `TORIDRAW_RASTER_KERNEL_FLAG_NONE` | Model order, with explicit back-face culling | Painter only; no z-buffer |
| `TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING` | ToriDraw's prepared face order | Painter only; no z-buffer |
| `TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER` | Model order, with explicit back-face culling | Model-local z-buffer |
| `TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING \| TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER` | ToriDraw's prepared face order | Model-local z-buffer |

ToriDraw reads `flags` only at pass boundaries, before face traversal. A sorting
flag makes the full-model entry points prepare and consume
`scene->tmp_face_order`; without it they walk the model's stored face order and
perform the back-face test that the sorter would otherwise perform. A z-buffer
flag provisions, rebases, and resets the scene's model-local depth buffer before
the first face; without it the pass does not depth-test.

Flags describe the whole kernel, not individual slots. There is no per-face
flag check or depth-mode selection. Painter vtables contain painter callbacks,
and depth vtables contain depth callbacks.

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

For a kernel carrying `TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER`,
`target->depth_test` is true and `target->zbuffer` is non-null for every
callback. For a painter kernel, `target->depth_test` is false and
`target->zbuffer` is null. These fields report the already-selected pass; stock
callbacks do not branch between painter and depth implementations through
them.

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
built-in normal kernel, a built-in smooth kernel, or an application kernel
completely specifies that choice. If an override needs distinct normal and
smooth behavior, expose two kernel objects or encode that distinction in its
`user_data`; callbacks cannot infer it from the target.

The explicit HD entry points are:

- `ToriDraw_RenderHDWithRasterKernel`; and
- `ToriDraw_RenderHDZBufferedWithRasterKernel`.

`ToriDraw_RenderModelWithRasterKernel` and
`ToriDraw_RenderHDWithRasterKernel` are the generic full-model entry points.
They project the model, then honor the selected kernel's flags exactly: any of
model-order painter, sorted painter, model-order depth, or sorted depth is
valid.

`ToriDraw_RenderModel3RasterWithRasterKernel` is the expert phase-three entry
point. It consumes the model already projected into the scene. If the kernel
needs face sorting, the caller must also have prepared the current face order
with `ToriDraw_RenderModel2SortFaces`; phase three does not repeat projection or
sorting. It still chooses painter versus depth rasterization from the kernel's
z-buffer flag.

`ToriDraw_RenderZBufferedWithRasterKernel` and
`ToriDraw_RenderHDZBufferedWithRasterKernel` require
`TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER`. This is a programmer precondition,
asserted in assertion-enabled builds. They also honor
`TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING`: z-buffered does not imply
model-order traversal when an explicit kernel asks for sorted depth.

The kernel pointer is an argument to the call. It is not stored on
`ToriDraw_Scene`, `ToriDraw_Model`, or `ToriDraw_ModelHD`, so two consecutive
calls can use different variants without mutation or synchronization of model
state.

ToriDraw still owns projection, sorting, culling, and depth-buffer setup; the
flags only select that pass-wide orchestration. Picking, model-priority policy,
and visibility limits remain outside the callback interface. A kernel cannot
recover a face discarded before face preparation.

## Built-in kernels and compatibility wrappers

ToriDraw exposes nine immutable, process-lifetime built-in kernels. The six
painter kernels carry `TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING`: SD
provides normal and smooth implementations of its branching and scanline
families, and HD provides branching and scanline implementations. The three
depth kernels carry `TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER`: SD provides
normal and smooth objects, and HD provides one object.

```c
const struct ToriDraw_RasterKernelSD *ToriDraw_RasterKernelSDGetBranching(void);
const struct ToriDraw_RasterKernelSD *ToriDraw_RasterKernelSDGetScanline(void);
const struct ToriDraw_RasterKernelSD *ToriDraw_RasterKernelSDGetSmoothBranching(void);
const struct ToriDraw_RasterKernelSD *ToriDraw_RasterKernelSDGetSmoothScanline(void);
const struct ToriDraw_RasterKernelSD *ToriDraw_RasterKernelSDGetZBuffered(void);
const struct ToriDraw_RasterKernelSD *ToriDraw_RasterKernelSDGetSmoothZBuffered(void);
const struct ToriDraw_RasterKernelHD *ToriDraw_RasterKernelHDGetBranching(void);
const struct ToriDraw_RasterKernelHD *ToriDraw_RasterKernelHDGetScanline(void);
const struct ToriDraw_RasterKernelHD *ToriDraw_RasterKernelHDGetZBuffered(void);
```

Painter and depth built-ins use different callbacks and vtables. A painter
callback never chooses a depth terminal at runtime, and a depth callback never
chooses a painter terminal. Sorting is likewise chosen once from `flags` before
the traversal; it is not tested by a face callback.

The legacy render functions choose a matching static mode-specific kernel once
per call. `ToriDraw_RenderModel` and `ToriDraw_RenderModel3Raster` retain the
existing model/scene depth policy by selecting either a sorted painter kernel
or an internal sorted-depth kernel. Model-sprite helpers use a sorted normal SD
painter kernel. `ToriDraw_RenderZBuffered` selects
`ToriDraw_RasterKernelSDGetZBuffered` or its smooth counterpart, both of which
request model-order depth. `ToriDraw_RenderHD` selects a sorted HD painter
kernel, while `ToriDraw_RenderHDZBuffered` selects the model-order HD depth
kernel.

The legacy `smooth` argument on `ToriDraw_RenderModel3Raster` and
`ToriDraw_RenderZBuffered` exists only to choose the corresponding static SD
kernel; it is not forwarded as raster state.

`ToriDraw_RasterSetScanline(bool)` changes that compatibility choice, and
`ToriDraw_Init()` recognizes `TORIDRAW_RASTER_SCANLINE=1`. Explicit-kernel calls
are unaffected. New code that needs per-object or per-call selection should
pass one of the built-in kernel pointers directly instead of changing the
global selector.

## Complete kernels and overrides

Every SD kernel supplies all four SD callbacks, and every HD kernel supplies
all six HD callbacks. Assertion-enabled builds assert that the kernel, vtable,
every callback, and flag bits satisfy the public contract before entering the
face loop. Release builds compiled with `NDEBUG` assume those invariants and do
not scan the vtable or flags for validity. To suppress a face class
deliberately, install a no-op callback; do not leave its slot null.

Every callback receives the `user_data` of the one selected kernel. ToriDraw
does not combine kernel objects or select callback-specific state. Built-in
callbacks ignore `user_data`. An application can therefore copy one complete
built-in vtable, replace the desired array slots, and use its own `user_data`;
the result is still one self-contained kernel with one direct dispatch per
face.

### Complete SD override

This example copies a selected built-in vtable once, then replaces the flat
slot with an application callback that counts and suppresses flat faces. The
other three slots remain the selected built-in implementations:

```c
struct FlatCounter {
    int count;
};

static void
counted_flat(void *opaque,
             const struct ToriDraw_RasterTarget *target,
             const struct ToriDraw_RasterFaceSD *face)
{
    struct FlatCounter *counter = opaque;
    (void)target;
    (void)face;
    counter->count++;
}

/* In the calling function: */
const struct ToriDraw_RasterKernelSD *built_in;
if (smooth) {
    built_in = use_scanline
        ? ToriDraw_RasterKernelSDGetSmoothScanline()
        : ToriDraw_RasterKernelSDGetSmoothBranching();
} else {
    built_in = use_scanline
        ? ToriDraw_RasterKernelSDGetScanline()
        : ToriDraw_RasterKernelSDGetBranching();
}

struct ToriDraw_RasterKernelSDVTable counter_vtable = *built_in->vtable;
counter_vtable.draw[TORIDRAW_RASTER_FACE_SD_FLAT] = counted_flat;

struct FlatCounter counter = { 0 };
struct ToriDraw_RasterKernelSD kernel = {
    .vtable = &counter_vtable,
    .user_data = &counter,
    .flags = built_in->flags,
};

int result = ToriDraw_RenderModelWithRasterKernel(
    model, scene, &position, &viewport, &camera, pixels, &kernel);
```

An HD override uses the same pattern: copy one complete built-in HD vtable and
replace, for example, `draw[TORIDRAW_RASTER_FACE_HD_CUBE]`. The copied solid,
plane, cylinder, and sphere callbacks ignore the application kernel's
`user_data`; the replacement receives it normally. Copy the built-in flags to
retain its orchestration, or assign one of the four documented flag
combinations when the replacement vtable implements that mode.

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
    .draw = {
        [TORIDRAW_RASTER_FACE_SD_GOURAUD] = draw_wireframe,
        [TORIDRAW_RASTER_FACE_SD_FLAT] = draw_wireframe,
        [TORIDRAW_RASTER_FACE_SD_TEXTURED] = draw_wireframe,
        [TORIDRAW_RASTER_FACE_SD_TEXTURED_FLAT] = draw_wireframe,
    },
};

struct ToriDraw_RasterKernelSD wire = {
    .vtable = &wire_vtable,
    .user_data = &wire_state,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING,
};
```

This kernel is a complete sorted-painter wireframe. A depth-aware wireframe
must provide callbacks that always update/test the supplied z-buffer and set
`TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER`, optionally combined with the
sorting flag. HD wireframe support needs a separate HD callback and all six HD
slots because the face descriptor type is different.

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
before rendering with `ToriDraw_SceneZBufferResize`; otherwise a render whose
kernel needs a z-buffer may grow it on first use. It never grows per face.

Kernel objects themselves contain no scene scratch. Immutable kernels can be
shared, provided any application `user_data` follows the caller's threading
rules and remains valid for every active call.

## `TORIDRAW_PIXEL16`

The typed public interface and all mode-specific built-in kernel objects remain
available in a 16-bit-pixel build, with these behavior differences:

- SD rendering works, but texture rasterization is compiled out. Faces that
  carry textures are classified as normalized Gouraud or flat SD faces, so the
  textured SD slots are complete but unreachable from production
  classification.
- The depth-tested 32-bit family is absent. Any SD kernel carrying
  `TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER` is unsupported: a render asserts
  in an assertion-enabled build and returns `TORIDRAW_CULL_ERROR` otherwise.
  Model depth flags are inert in the legacy ordinary flow.
- The built-in HD kernels remain valid complete typed objects, but all HD and
  HD-Z render entry points report `TORIDRAW_CULL_ERROR`, clear supplied HD
  statistics, and do not invoke application callbacks.

An SD override intended for both pixel formats should use a painter flag
combination, implement meaningful Gouraud and flat behavior, and must not
assume a texture descriptor is active for those classes.

## Performance contract

The implementation keeps dispatch outside inner raster loops:

1. Before traversal, assertion-enabled builds assert the typed kernel, its four
   or six slots, and its known flags. `NDEBUG` builds omit those checks.
2. Read `kernel->flags` only at pass boundaries, then prepare the selected
   traversal and optional depth target before the face loop.
3. Prepare one stack descriptor for each drawable face.
4. Index `kernel->vtable->draw[face.face_class]` and make one indirect callback,
   passing that kernel's `user_data`.
5. Let the selected callback enter direct triangle, span, and pixel code.

ToriDraw does not prepare or copy a callback table. There is no release-time
completeness/flag scan, face-class switch, per-face validation, per-face flag or
depth-mode branch, scratch-field exchange, or synchronization branch. The
selected kernel is passed down and indexed directly. Dispatch allocates no heap
storage. Separate lazy texture-state or z-buffer provisioning may allocate
before the face loop and can be paid at startup when required. Compatibility
inputs are read once when their wrapper chooses a built-in kernel. Normal
versus smooth SD Gouraud and painter versus depth rasterization are both encoded
by that selected mode-specific kernel, not carried through the face loop.
Runtime choices inside a built-in callback are remaining semantic modifiers
such as affine mapping, gate, alpha, and modulation.

## Acceptance criteria

The kernel boundary is correct when tests establish all of the following:

- every drawable SD class reaches exactly one of four typed slots;
- every drawable HD class reaches exactly one of six typed slots;
- flat shades, opacity, vertex indices, texture gates, frames/mappings, clip
  state, and target arrays are normalized as documented;
- all nine public built-in kernels preserve their corresponding painter or
  depth legacy output through specialized vtables;
- all four valid flag combinations produce their documented traversal and
  depth setup, with flags read before rather than during face dispatch;
- incomplete kernels and unknown flag bits assert in assertion-enabled builds;
- the explicit Z-buffered kernel entry points require the z-buffer flag and
  honor the optional sorting flag;
- changing the kernel argument affects only that call and writes no scene or
  model kernel state; and
- Pixel16 follows the reduced behavior above.

The relevant implementation is in
[`toridraw_raster_kernel.h`](../3rd/toridraw/toridraw_raster_kernel.h),
[`toridraw_raster_kernel_internal.h`](../3rd/toridraw/toridraw_raster_kernel_internal.h),
[`toridraw_raster.u.c`](../3rd/toridraw/toridraw_raster.u.c), and
[`toridraw_render_hd.u.c`](../3rd/toridraw/toridraw_render_hd.u.c).
