# ToriDraw raster kernels

Status: implemented architecture and migration record. HD near-plane
reconstruction and workload-level performance measurement remain documented
follow-ups; neither changes the public dispatch contract.

## Purpose

ToriDraw should choose its model-face raster implementation through a
`ToriDraw_RasterKernel` interface. A scene can then select a complete raster
family at runtime or layer sparse face-type overrides over an existing family.

The design has four priorities:

1. Preserve the behavior of every face currently accepted by the stock model
   raster path.
2. Make a runtime variant switch local to a scene and stable for one model
   raster pass.
3. Pay one interface dispatch per drawable face, with no kernel dispatch in
   scanline, span, or pixel loops.
4. Make partial overrides representation-safe: an override receives normalized,
   debug-checked face data and inherits every slot it does not replace.

This is an interface extraction first. It must not silently change alpha,
texture, clipping, sorting, or depth behavior.

## Scope

The implementation covers the normal software model raster flow:

```text
ToriDraw_RenderModel
  -> ToriDraw_RenderModel1Project
  -> ToriDraw_RenderModel2SortFaces
  -> ToriDraw_RenderModel3Raster
  -> ToriDraw_RasterWithFaceIndices
  -> prepared face
  -> selected ToriDraw_RasterKernel slot
```

It also covers the unsorted face loop used by `ToriDraw_RenderZBuffered` and the
base model fields of both `TORIDRAWMK_MODEL` and `TORIDRAWMK_MODEL_HD` when they
pass through that normal flow.

`ToriDraw_RenderHD` and `ToriDraw_RenderHDZBuffered` use the same four-slot
interface through HD-domain terminal roots while retaining their material,
alpha, missing-material, mapping, tint, statistics, and depth policies.
`TORIDRAW_PIXEL16` exposes the same public object/vtable layout and stock
dispatch, while its HD render entry points return the defined unsupported
result.

The following are deliberately separate:

- HD near-plane reconstruction remains incomplete in the underlying PMN/mapped
  families and is a separately reviewed follow-up; the interface does not claim
  to repair it.
- `ToriDraw_ModelGround` is consumed by the ground/GPU/packet paths and is not
  accepted by the current software `ToriDraw_Raster` entry point.
- Low-level Gouraud-RGB triangle kernels are implementation families, not a
  semantic class emitted by the current model face classifier, so they do not
  require a fifth public slot.
- Sorting, priority buckets, projection, picking, and lighting are not kernel
  services. In particular, a kernel cannot recover a face dropped by an
  upstream sort limit.
- This change does not make ToriDraw reentrant. Existing global near-clip
  scratch and other shared state remain separate work.

## Why the interface belongs at the face boundary

The current switch in
[`toridraw_raster.u.c`](../3rd/toridraw/toridraw_raster.u.c) combines several
jobs: interpreting encoded model data, skipping non-drawable faces, resolving a
texture, selecting one of four semantic face classes, and calling a triangle
family. The current runtime scanline selection then branches again in triangle
wrappers.

The useful extension point is after interpretation but before triangle setup.
It is frequent enough to replace a face implementation, but high enough that
the hot span and pixel loops remain direct calls. It also prevents every custom
kernel from having to rediscover rules such as whether P/M/N are vertex indices
or texture-mapping data.

Raw `face_infos` values are not vtable indices. They encode lighting and hidden
face policy, while the drawable raster classes are:

| Semantic class | Untextured | Textured | Shade selection |
|---|---:|---:|---|
| Gouraud | yes | no | three prepared shades |
| Flat | yes | no | flat-colour sentinel |
| Textured | no | yes | three prepared shades |
| Textured flat | no | yes | flat-colour sentinel |

Depth testing, near clipping, affine versus perspective mapping, and opaque
versus colour-keyed textures are modifiers within those classes. Making each
combination a public vtable slot would expose a large, unstable matrix and make
partial overrides awkward.

## Public interface

The implementation adds a public raster-kernel header and exposes these names
through [`toridraw.h`](../3rd/toridraw/toridraw.h). The callback descriptors and
HD mapping record are concrete public read-only structures, not opaque handles.
The object and vtable shape is deliberately small:

```c
struct ToriDraw_RasterKernel;
struct ToriDraw_RasterTarget;
struct ToriDraw_RasterFace;
/* ToriDraw_TexMapping is defined by toridraw_texture_mapping.h. */

enum ToriDraw_RasterFaceClass
{
    TORIDRAW_RASTER_FACE_GOURAUD = 0,
    TORIDRAW_RASTER_FACE_FLAT = 1,
    TORIDRAW_RASTER_FACE_TEXTURED = 2,
    TORIDRAW_RASTER_FACE_TEXTURED_FLAT = 3,
    TORIDRAW_RASTER_FACE_CLASS_COUNT = 4,
};

enum ToriDraw_RasterKernelDomain
{
    TORIDRAW_RASTER_KERNEL_STOCK = 1u << 0,
    TORIDRAW_RASTER_KERNEL_HD = 1u << 1,
};

enum ToriDraw_RasterTextureGate
{
    TORIDRAW_RASTER_TEXTURE_OPAQUE = 0,
    TORIDRAW_RASTER_TEXTURE_COLOR_KEY = 1,
    TORIDRAW_RASTER_TEXTURE_TEXEL_ALPHA = 2,
};

enum ToriDraw_RasterMappingPayload
{
    TORIDRAW_RASTER_MAPPING_VERTEX_FRAME = 0,
    TORIDRAW_RASTER_MAPPING_STOCK_FACE_FALLBACK = 1,
    TORIDRAW_RASTER_MAPPING_HD = 2,
    TORIDRAW_RASTER_MAPPING_HD_FRAME_FALLBACK = 3,
};

typedef void (*ToriDraw_RasterKernelFaceFn)(
    void *user_data,
    const struct ToriDraw_RasterTarget *target,
    const struct ToriDraw_RasterFace *face);

struct ToriDraw_RasterKernelVTable
{
    ToriDraw_RasterKernelFaceFn draw_gouraud;
    ToriDraw_RasterKernelFaceFn draw_flat;
    ToriDraw_RasterKernelFaceFn draw_textured;
    ToriDraw_RasterKernelFaceFn draw_textured_flat;
};

struct ToriDraw_RasterKernel
{
    const struct ToriDraw_RasterKernelVTable *vtable;
    void *user_data;
    const struct ToriDraw_RasterKernel *fallback;
    unsigned int domains;
};
```

The four callbacks intentionally share one signature. This keeps fallback
resolution and instrumentation simple and allows a single override callback to
cover multiple classes when desired.

Built-in roots are immutable, process-lifetime objects:

```c
const struct ToriDraw_RasterKernel *
ToriDraw_RasterKernelGetBranching(void);

const struct ToriDraw_RasterKernel *
ToriDraw_RasterKernelGetScanline(void);
```

Scene selection is explicit:

```c
bool
ToriDraw_SceneSetRasterKernel(
    struct ToriDraw_Scene *scene,
    const struct ToriDraw_RasterKernel *kernel);

bool
ToriDraw_SceneResetRasterKernel(struct ToriDraw_Scene *scene);

const struct ToriDraw_RasterKernel *
ToriDraw_SceneGetRasterKernel(const struct ToriDraw_Scene *scene);
```

The binding lives on the scene because callers are allowed to run projection,
sorting, and raster phases separately, and phase 3 receives the scene rather
than the original model arguments. Adding a kernel only to the convenience
`ToriDraw_RenderModel` call would leave those callers on a different dispatch
path.

`Set` requires a non-null, acyclic, structurally valid chain. It returns false
and leaves the old binding untouched if validation fails or a raster pass is
active. `Reset` has the same active-pass rule and stores null on success,
meaning "inherit the render entry point's terminal root." `Get` returns the
explicit binding and may therefore return null. There is intentionally no
context-free "effective" getter: stock and HD entry points supply different
terminals.

Append the kernel pointer and an internal active-pass flag to the end of
`ToriDraw_Scene`. Appending avoids disturbing the assembly-sensitive offsets at
the beginning of that public structure. Every scene allocation and
buffer-initialization path must initialize both fields.

### Ownership and mutation

The scene borrows the kernel, vtable, fallback chain, and `user_data`; it never
copies or frees them. They must outlive every scene binding and every active
raster pass that uses them. Vtables should normally be `static const`.

Changing a scene binding is allowed only between model raster passes. Set/reset
from a callback is rejected; otherwise a callback could retire `user_data` that
another resolved slot still needs. At pass entry ToriDraw snapshots the binding
and terminal root. Concurrent mutation or rendering of the same scene requires
external synchronization.

A callback may render a different scene. A recursive convenience render of the
active scene is rejected before projection or sorting can overwrite the outer
pass's scratch. The split projection/sort APIs remain low-level phase controls;
callers must not invoke those individual phases recursively on an active scene.

Set the active flag before resolving and clear it through one cleanup path on
every return, including an empty model or invalid live chain, so a failed pass
cannot permanently lock the scene binding.

Version 1 has no destructor or ownership callback. The repository builds
ToriDraw from source, including its unity translation unit, rather than loading
binary raster plugins. `abi_version` and `struct_size` fields should be added
only if binary compatibility becomes a real requirement. Source consumers must
be rebuilt when these concrete public structures change; keep the vtable layout
stable and identical in normal and `TORIDRAW_PIXEL16` builds.

## Sparse overrides and fallback

A null vtable slot means "continue down the explicit `fallback` chain for this
face class." If the chain ends before supplying the slot, resolution continues
through the render entry point's terminal root. It does not mean "skip this
face." To suppress a class, install an explicit no-op callback. Built-in roots
have all four slots populated and a null fallback.

The resolver ignores a node whose `domains` mask does not include the current
stock or HD pass. This lets one scene carry a stock-only, HD-only, or shared
override safely. Built-in stock roots advertise only `STOCK`; HD roots
advertise only `HD`. A zero or unknown domain mask is invalid.

For example, an application can replace flat faces and inherit everything else:

```c
static const struct ToriDraw_RasterKernelVTable outline_vtable = {
    .draw_flat = draw_flat_outline,
};

static struct OutlineState outline_state;
static struct ToriDraw_RasterKernel outline_kernel;

static void
init_outline_kernel(void)
{
    outline_kernel = (struct ToriDraw_RasterKernel){
        .vtable = &outline_vtable,
        .user_data = &outline_state,
        .fallback = NULL,
        .domains = TORIDRAW_RASTER_KERNEL_STOCK |
                   TORIDRAW_RASTER_KERNEL_HD,
    };
}
```

With a null explicit fallback, this override inherits the stock terminal from a
stock call and the HD terminal from an HD call. To pin its inherited stock slots
to one variant, set `fallback` to `ToriDraw_RasterKernelGetBranching()` or
`ToriDraw_RasterKernelGetScanline()` and restrict its domain accordingly. The
vtable can be `static const`; initialize the kernel once and then treat the
whole chain as immutable while bound.

Each resolved slot is a pair of callback and `user_data`. The `user_data` comes
from the chain node that supplied that callback, not necessarily from the head
kernel. This is necessary for multiple independent override layers.

At the start of `ToriDraw_RasterWithFaceIndices`, resolve the explicit chain and
the entry-point terminal into a small stack object:

```c
struct ToriDraw_ResolvedRasterSlot
{
    ToriDraw_RasterKernelFaceFn function;
    void *user_data;
};

struct ToriDraw_ResolvedRasterKernel
{
    struct ToriDraw_ResolvedRasterSlot slots[TORIDRAW_RASTER_FACE_CLASS_COUNT];
};
```

Resolution performs no allocation. It detects fallback cycles, validates every
node, skips domain-incompatible nodes, and fills any remaining slots from the
complete internal terminal. Cycle detection can use tortoise/hare traversal
before the four slot walks. The inner face loop then contains neither
null/fallback checks nor a global variant check.

Set-time validation gives callers an error result. Pass-time validation is
still required because live borrowed nodes could have been structurally mutated
in violation of the contract. If that happens, the resolver emits a diagnostic
before the face loop, discards the entire explicit chain, and uses the
entry-point terminal. It never loops forever, calls a null slot, or draws
through a partially resolved custom chain. This recovery assumes the chain
storage is still readable. Freeing a bound node, vtable, or `user_data` is a
lifetime violation that no pointer-based C API can detect safely.

The public vtable keeps named members for readable initializers; the resolved
form uses the stable face-class enum as an array index. The core guarantees the
class range before the indexed load.

Version 1 treats an override as a complete replacement for its semantic class.
A public "call next implementation" operation is intentionally omitted. It
would require retaining the chain position in every callback and would add
complexity to the hot contract. A callback may inspect the prepared face and
choose its replacement draw implementation, but it cannot mutate the const
descriptor or invoke the next slot. Add explicit delegation only after a
concrete wrapper use case requires it.

## Prepared callback data

Callbacks receive borrowed, read-only descriptors. ToriDraw owns their storage;
a callback must not retain either pointer beyond the call.

Use concrete public structures with a compact common header and a tagged
texture payload. Do not expose an opaque type unless the same change also adds
a complete accessor API; otherwise an out-of-tree override could not do useful
work. Conversely, raw face-info, colour-sentinel, alpha, and texture-coordinate
arrays stay in the private core context so callbacks do not need to decode them.

### `ToriDraw_RasterTarget`

This descriptor is constructed once and is stable for the model pass. Its
public fields provide:

- the active `ToriDraw_RasterKernelDomain`;
- framebuffer and optional depth-buffer pointers;
- width, height, stride, viewport/clip origin, and projection centre;
- camera/near-plane constants and parallel-versus-perspective projection;
- pass flags for smooth shading, affine textures, depth testing, and available
  near-clip data;
- projected screen/depth arrays and orthographic arrays used by clipping;
- model-space position arrays required by a prepared mapping payload.

The descriptor is logically const, but the framebuffer and depth buffer it
points to are writable render targets. A reserved ToriDraw-private context
pointer may let built-in callbacks reach incremental-migration state, but it is
not part of the external override contract. Mutable implementation details such
as the one-entry texture cache, current material table, and routing/debug
counters remain owned by that core pass context rather than becoming
kernel-global state.

### `ToriDraw_RasterFace`

This descriptor is reused for one drawable face. Its common header contains:

- semantic face class and original face index;
- model-contract A/B/C vertex indices, with the existing debug-level bounds
  diagnostics;
- `shade[3]`, always normalized to A/B/C values; a flat class gets A/A/A and
  never exposes `TORIDRAWHSL16_FLAT` as an active shade;
- effective source opacity from 0 through 255, where 255 is opaque;
- whether the source triangle contains the near-clip sentinel.

For stock untextured faces, effective opacity is 255 when the alpha array is
absent and otherwise `255 - raw_alpha`; faces at the current cutoff never reach
a callback. For stock textured faces it is always 255, preserving the current
ignored-face-alpha behavior. HD preparation supplies the effective HD face
opacity instead. The raw byte is not exposed as a second competing meaning.
Untextured shades are prepared HSL16 palette words; textured shades are the
prepared lightness values expected by the texture families. The semantic class
therefore determines how to interpret the same three integer fields.

The active tagged texture payload, present only for `TEXTURED` and
`TEXTURED_FLAT`, contains:

- texture id, resolved texels, dimensions, and address/clamp state;
- a normalized gate enum: opaque, RGB-zero colour key, or texel alpha;
- the original texture render-type byte;
- a mapping-payload tag and union:
  - `VERTEX_FRAME`: valid type-0 P/M/N vertex indices;
  - `STOCK_FACE_FALLBACK`: safe A/B/C indices for an absent/normalized-`-1`
    coordinate or a nonzero render type, while retaining the byte that controls
    affine routing;
  - `HD_MAPPING`: a non-null `const struct ToriDraw_TexMapping *` into the
    `MODEL_HD` mapping tail for HD types 1-3; its prepared mapping fields are
    public through `toridraw_texture_mapping.h`;
  - `HD_FRAME_FALLBACK`: three bounds-checked vertex indices reused by the
    current HD missing-mapping fallback;
- prepared HD-only sampler fields such as modulation/tint when the target
  domain is HD. Face coverage always uses the common effective-opacity field;
  do not add a second alpha representation.

Base `MODEL` objects have no decoded HD mapping. A normal stock render of a
`MODEL_HD` also uses only `VERTEX_FRAME` or `STOCK_FACE_FALLBACK`; it must not
consume the HD tail. HD type 0 prepares its frame from the bind/original
positions, while HD types 1-3 use `HD_MAPPING`. This policy-tagged union prevents
negative axis/mapping values from being mistaken for vertex indices.

This extraction does not add a new release-mode malformed-model policy. Valid
decoded A/B/C indices remain part of the existing model contract, the sort may
already have dereferenced them, and debug checks continue to report violations.
The descriptors are representation-safe, not a sandbox: a callback can still
misuse a valid array and index. The guarantee is that the core never labels raw
mapping-axis data as a vertex-index payload.

Keep the face descriptor compact: common fields plus the active union, with all
pass-stable arrays in `Target`. Reuse one stack instance and assign the active
fields instead of clearing/copying a large structure for every face. Scanline
accumulators, sampler inner-loop state, and the HD compositing table remain
private to concrete kernels.

## Core and kernel responsibilities

The boundary is intentionally strict:

| Core dispatcher owns | Selected kernel owns |
|---|---|
| face order and priority-sort results | final face/near-clip entry point |
| pre-clip backface culling and near-clip exemption | clipped-polygon winding and triangle setup |
| raw face-info interpretation | affine/perspective triangle selection |
| hidden/invalid/alpha skip policy | opaque/keyed triangle selection |
| texture lookup and one-entry cache | scanline/span family below the face call |
| semantic class selection | writing colour/depth targets |
| descriptor normalization and routing/skip counters | implementation-local debug/perf counters |
| per-model depth-buffer reset | |

This gives an override the same normalized input that a built-in receives while
ensuring that alternate kernels cannot accidentally disagree about which raw
faces exist or confuse a mapping record with vertex indices.

The existing debug `drawn` counters count successful routing to a face
implementation, not proven pixel writes; clipped and degenerate triangles
already make that distinction. Rename or document them as `dispatched` when the
interface lands. An explicit no-op therefore counts as one dispatch and zero
implementation-local writes. Preserve `TORIDRAW_RASTER_DEBUG` accounting and
the `TORIDRAW_SKIP_TEXTURED` bisect knob during extraction; retire either only
in a separately documented cleanup.

Hidden faces, invalid raw types, alpha skips, and missing stock textures are not
override slots because no drawable face exists after policy. If a later use
case needs to replace those decisions, add a separately named cold
classification/preparation hook; do not overload a draw callback or turn the
raw byte into a vtable index.

### Required stock behavior

The refactor must preserve the current valid-model decisions in
[`ToriDraw_RasterModelFace`](../3rd/toridraw/toridraw_raster.u.c), with the named
malformed-coordinate hardening called out below:

| Input condition | Required result |
|---|---|
| face omitted by sorted culling/order, or rejected by the unsorted front-face test | no callback |
| raw face type 2, or a raw type outside 0-3 | no callback |
| raw face type 3 | continue through the existing colour, texture, and alpha policy; do not skip on the raw value alone |
| hidden-colour sentinel | no callback |
| untextured effective opacity `255 - face_alpha <= 1` | no callback |
| stock textured face whose texture cannot be resolved | no callback |
| malformed explicit texture coordinate or unsafe prepared frame indices | no callback and a named diagnostic count |
| a near-clipped face for which the pass has no required prepared coordinates | no callback |
| untextured, non-flat face | exactly one `draw_gouraud` call |
| untextured flat-sentinel face | exactly one `draw_flat` call |
| textured, non-flat face | exactly one `draw_textured` call |
| textured flat-sentinel face | exactly one `draw_textured_flat` call |

Lighting has special signed-alpha handling for values 254 and 255 before this
stage. Preserve the resulting hidden/special semantics; do not simplify raw
face type and alpha into a new public enum earlier in the pipeline.

The default matrix is also contractual: absent `face_infos` means raw type 0
and absent alpha means effective opacity 255. In a normal non-Pixel16 build,
only texture id `-1` selects the untextured route. During lighting, signed alpha
255 becomes hidden type 2 and 254 becomes special type 3: an untextured type-3
face becomes flat black and is normally removed by the later opacity cutoff,
while a textured type-3 face is lit hidden. Ordinary textured alpha is ignored
only after those lighting effects have already happened. Authored raw type 3
with no alpha is therefore not universally hidden and may draw.

Current stock textured rendering ignores per-face alpha, including its
depth-tested path, while untextured rendering applies it. Missing stock
textures are skipped. These are compatibility requirements for this extraction,
not endorsements. Any alpha or fallback correction needs its own visual review
and tests.

For texture render type 0, a valid texture-coordinate record may provide P/M/N
vertex indices. Every nonzero byte carries non-vertex data in the stock flow;
the stock built-ins must continue their current affine A/B/C fallback and must
not accidentally activate the HD cylinder/cube/sphere projections. Preserve
the original byte in the tagged payload. An absent or adapter-normalized `-1`
texture coordinate falls back to face A/B/C. Preserve the current edge case
where that internal fallback coordinate can still consult
`texture_render_types[face]` when the face index is in range, which may force
affine routing.

An explicit out-of-range coordinate on a directly constructed model is not a
current fallback case: the stock helper treats it as a plane record and indexes
P/M/N out of bounds. Decoders normally normalize it to `-1`. The new preparer
must reject/count this malformed condition before the callback rather than
publish unsafe indices. This is a documented safety hardening, not pixel-parity
behavior for valid decoded models.

Cache/model adapters normalize texture-coordinate bytes separately (including
255/out-of-range to `-1`); this interface must not normalize them a second,
different way. There is also an upstream merge hazard where P/M/N rebasing can
touch nonzero-type axis data. The interface tags that data safely but does not
repair model merging as part of this extraction.

Sorted and unsorted/depth-tested loops must perform equivalent front-face
culling. A face with the near-clipped sentinel bypasses the early cull because
the clipped polygon establishes its own winding. Depth-buffer reset remains
once per model, outside the kernel chain.

Stock near-clip capability is a model-level quirk:
`ToriDraw_ModelHasTextures`/`textured_face_count > 0` decides whether the
orthographic scratch exists, not whether the current face is textured. Thus an
untextured face in a model with texture records can be clipped, while the same
face in a wholly untextured model is dropped. Core dispatch owns that
availability test and the pre-clip cull exemption; the selected face wrapper
owns clipping the polygon and testing the rebuilt winding.

Today the unavailable-data case is counted early only when raster debugging is
enabled and otherwise reaches a triangle wrapper that returns without pixels.
The interface makes it an unconditional named core skip so a custom callback is
never handed an unusable face. This changes new dispatch observability, not
pixels, and belongs in the Phase 0 oracle and routing test.

## Runtime variant selection

Today `ToriDraw_RasterSetScanline` changes a process-global boolean, and several
triangle wrappers consult it. During migration:

1. Keep `ToriDraw_RasterSetScanline` and `ToriDraw_RasterGetScanline` as
   deprecated compatibility APIs.
2. Treat them as selection of the stock entry-point terminal. Continue to honor
   `TORIDRAW_RASTER_SCANLINE` during `ToriDraw_Init`.
3. Snapshot the applicable terminal once at pass entry. A compatible explicit
   chain overlays it; a complete compatible root pins all four slots.
4. Give branching and scanline roots separate callbacks that directly call
   their triangle families. Until this split exists, the two root objects must
   not be advertised as independently selectable because both still reach leaf
   wrappers governed by the global.
5. Migrate HD untextured/missing-material fallbacks and audit any other library
   consumer of selector-reading wrappers. Keep
   `toridraw_scanline_parity_test.c` unchanged as the raw-family oracle: it calls
   branching/scanline symbols directly. Keep
   `src/render/test/scanline_compare_sdl.c` working through the compatibility
   setter for an unpinned scene, and optionally add an explicit-root mode.
6. Only then remove the global read from leaf wrappers and narrow the old API's
   comment to "select the terminal used by unpinned raster passes."

This preserves legacy direct callers during migration and ultimately permits
two scenes to render with different variants in the same process. A sparse
override with no explicit fallback follows later process-default changes at the
next pass; a chain terminating in a complete root does not.

## HD integration

[`toridraw_render_hd.u.c`](../3rd/toridraw/toridraw_render_hd.u.c) already has a
large private matrix over projection, texture gate, face alpha, modulation, and
depth testing. That matrix should not become dozens of public vtable slots.

The completed interface adds two domain-specific roots. Their textured slots
share the same HD matrices; their flat/Gouraud slots preserve whether the
snapshotted process terminal selected branching or scanline:

```c
const struct ToriDraw_RasterKernel *
ToriDraw_RasterKernelGetHDBranching(void);

const struct ToriDraw_RasterKernel *
ToriDraw_RasterKernelGetHDScanline(void);
```

An unpinned HD pass snapshots the same process family selection and chooses the
matching HD terminal, preserving the current effect of
`ToriDraw_RasterSetScanline` on HD solid and missing-material fallbacks.

After the stock path is stable, integrate HD as follows:

1. Share structural face normalization/debug checks and the four semantic
   callback classes where their meaning is identical.
2. Keep per-call `materials` and `out_stats` in the HD pass context. Prepare the
   resolved sampler/mapping payload before invoking a callback; they are not
   persistent kernel `user_data`.
3. Implement the HD roots whose textured callbacks dispatch through the
   existing private matrices and whose solid callbacks directly select their
   named family.
4. Keep `ToriDraw_RenderHD` and `ToriDraw_RenderHDZBuffered` as compatibility
   facades.
5. Supply the matching HD root as the terminal for an HD pass. Compatible
   explicit HD/shared-domain slots overlay it; stock-only nodes are ignored.
   Therefore a sparse override can safely live on a scene used by both flows.

HD currently draws a visible flat/Gouraud fallback for a missing material and
honors face alpha on textured faces. Stock rendering skips a missing texture
and ignores textured face alpha. The adapter must retain that distinction; a
shared interface is not permission to merge policy. Prepare a missing-material
face as the appropriate solid semantic class before dispatch so the callback
sees normalized fallback data. Core HD route counters continue to describe
where the face was dispatched, including through a custom no-op.

HD has three more cold-policy rules that preparation must retain:

- if any lit `face_colors_a/b/c` array is absent, count the face as
  `skipped_hidden` and make no callback; stock continues its existing valid-model
  precondition instead;
- null texels or a material width other than 64/128 means missing material and
  takes the solid fallback, while an invalid gate coerces to opaque;
- modulation is derived from authored `model->face_colors[face]` HSL (or zero
  when that array is absent), never from the 0..127 lit textured shades. Publish
  the already prepared tint/sampler values to the callback.

HD also differs in mapping details. Type 0 uses the original/bind-pose P/M/N
frame and types 1-3 use the decoded HD mapping tail. When a mapping is missing,
current code coerces to the frame path and reuses that record's P/M/N as vertex
indices: it draws only when all three happen to be in range and otherwise
skips. Represent the successful case as `HD_FRAME_FALLBACK`; do not silently
replace it with A/B/C. Malformed render types above 3 currently coerce to type
0. Preserve each rule in the adapter, and treat any future A/B/C repair as a
separately reviewed behavior change.

Current HD near clipping is incomplete: its untextured fallback disables the
stock rebuild and its mapped/PMN kernels do not rebuild a near-plane polygon.
The cull loop nevertheless exempts sentinel faces. Record this as a known
baseline during the interface-only adapter, then implement and test HD
near-plane reconstruction as a separately reviewed subphase before claiming
that the common interface handles every HD face. Do not silently describe the
current incomplete behavior as supported.

## `TORIDRAW_PIXEL16`

The public object and vtable layout must compile unchanged with
`TORIDRAW_PIXEL16`; do not hide fields or reorder callbacks conditionally.
Pixel16 supports the stock domain only. The current unity build cannot compile
the real HD unit because HD embeds z-buffer types and 32-bit texture kernels
that Pixel16 excludes. Before adding its routing oracle, guard the real HD unit
and provide stable Pixel16 HD API/root stubs that report unsupported
(`TORIDRAW_CULL_ERROR` for render calls, zeroed optional stats) without entering
face dispatch.

The stock Pixel16 classifier is intentionally different: texture lookup and
both textured cases are compiled out. Every face that survives the common
raw-type, hidden-shade, untextured-opacity, and near-clip-availability policy
ignores texture presence and reaches only `draw_flat` or `draw_gouraud`
according to the final C shade. Missing textures do not skip in this build.

Preserve that collapse exactly. Complete roots still populate the two textured
slots with non-null unreachable assertion stubs so resolution has one layout,
but an ordinary Pixel16 model must never reach those stubs. The forced z-buffer
entry point keeps its existing assertion. Add a Pixel16 routing oracle instead
of assuming the normal-build matrix applies.

## Performance contract

For a valid, resolved stock kernel, the steady-state model loop should do:

```text
classify/prepare face -> load resolved slot -> one public indirect face call
                       -> direct triangle/scanline/span/pixel implementation
```

Required properties:

- kernel selection and fallback resolution happen once per model raster pass;
- no heap allocation occurs during resolution or per-face dispatch;
- each drawable face makes exactly one public interface call;
- no fallback, null-slot, or process-global family read remains in the callback
  body or any scanline/span/pixel descendant;
- texture lookup retains the current per-model one-entry cache;
- target state that is constant for the pass is prepared once, not copied for
  every face;
- the compact face descriptor does not force stores for inactive payload fields.

HD also makes one public interface call per source face. Its textured callback
may retain the existing private compositing/mapping table call once per emitted
triangle; an unclipped face emits one triangle and a rebuilt clipped polygon may
emit more. That is an explicit exception, not another public virtual layer.
Measure it independently and keep the private table out of spans and pixels.

Benchmark the real model path, not only direct triangle calls. Compare old and
new complete branching, complete scanline, sorted, and forced-zbuffer paths;
also compare a resolved sparse chain. Include tiny/low-face models (where
per-pass resolution dominates), untextured-heavy, texture-hit/miss mixtures,
opaque/keyed textures, and large QBD/Jad/wyrm stress fixtures. Measure descriptor
construction as well as the call itself.

Use paired batches of at least 2,000 frames, multiple repetitions, and medians;
record routed faces, pixels, and wall time. A repeatable regression over 2% with
run-to-run noise below that threshold is a redesign trigger. Inspect optimized
output to confirm that the public indirect call occurs only at the face
boundary and that no global family read remains below it.

## Migration record

The interface landed through the staged sequence below. The bullets retain the
original acceptance wording so future changes can be checked against the same
constraints. Phases 1-5 are implemented. Phase 0's routing oracles are present,
with broader image/performance fixtures still useful; Phase 6 is explicitly the
separate HD near-clip follow-up.

### Phase 0: lock down the oracle

- Add model-level routing fixtures before moving code out of
  `ToriDraw_RasterModelFace`.
- Capture branching pixel hashes for representative untextured, textured,
  clipped, depth-tested, smooth, and non-smooth models.
- Capture the current Pixel16 class collapse and the stock/HD mapping and alpha
  differences as routing oracles.
- First make Pixel16 a defined build again by guarding the real HD unity include
  and adding deterministic unsupported HD stubs; do not mistake today's compile
  failure for a render oracle.
- Record the existing direct-model out-of-range coordinate failure under a
  sanitizer so the named safety hardening is deliberate and measurable.
- Record the known scanline differences documented in
  [RASTER_VARIANT_CATALOGUE.md](RASTER_VARIANT_CATALOGUE.md) rather than treating
  every difference as a new failure.

### Phase 1: add the interface without changing dispatch

- Add the concrete public kernel/vtable, domain, target, face, gate, and mapping
  payload declarations.
- Append and initialize the nullable binding/active flag in `ToriDraw_Scene`,
  but keep scene binding APIs private until the resolver and independent roots
  are tested and ready to affect rendering.
- Implement and unit-test entry-point terminal injection, domain filtering,
  cycle detection, and pass-time safe fallback as internal machinery.
- Add the branching root internally, but do not expose branching/scanline
  getters while leaf wrappers still read the global.
- Leave the old face router in control until its behavior is covered.

### Phase 2: separate policy from drawing

- Split `ToriDraw_RasterModelFace` into a core classifier/preparer and four
  built-in draw callbacks.
- Preserve raw-type, hidden-colour, alpha, texture-cache, missing-texture, and
  valid-model P/M/N rules byte for byte, apart from the documented malformed
  coordinate rejection.
- Route both sorted and unsorted/depth-tested face loops through the internal
  complete branching root; do not consult the dormant scene binding yet.
- Make the complete branching root the stock terminal and compare it to the
  Phase 0 oracle, including normal `MODEL_HD` base rendering and Pixel16.

### Phase 3: extract runtime variants

- Give scanline and branching roots direct family-specific face callbacks.
- Migrate HD solid fallbacks off wrappers that consult the global at leaf level;
  retain the raw-family parity test and the unpinned compatibility compare tool.
- Convert the global scanline API into terminal selection for unpinned passes
  and remove all lower-level global reads.
- Finish the complete branching and scanline roots after their callbacks are
  genuinely independent, but keep their public getters gated with scene binding
  until Phase 4.
- Verify smooth/non-smooth, affine/perspective, near-clipped, z-buffered, and
  Pixel16 configurations before promising independent scene roots.

### Phase 4: enable sparse overrides

- Test multi-layer inheritance, entry-point terminal injection, supplying
  `user_data`, domain filtering, explicit no-op suppression, invalid domains,
  cycles, and live-chain mutation recovery before the public cutover.
- Export the built-in getters and validating bool-returning scene set/reset/get
  APIs together, then activate compatible scene bindings in both sorted and
  unsorted loops.
- Resolve slots once per pass and fill holes from the snapshotted terminal.
- Verify independent bindings for two scenes, process-default changes between
  passes, pinned-root behavior, and rejection of set/reset during a pass.

### Phase 5: adapt HD without policy drift

- Add branching/scanline HD roots using the existing private texture tables and
  direct solid-family fallbacks.
- Move per-call materials, sampler preparation, and stats through the HD target
  and face preparation contract, not kernel lifetime state.
- Keep stock and HD texture/alpha fallback policy separate.
- Preserve malformed mapping, missing material, and missing mapping behavior;
  keep the private HD matrix's extra indirect call explicit and measured.
- Capture the current near-clip limitation rather than changing it inside the
  interface extraction.

### Phase 6: close HD near clipping and clean up

- Implement and test HD near-plane polygon reconstruction for solid, PMN, and
  mapped textured faces as a separately reviewed behavior change.
- Delete obsolete monolithic routing only after all parity and performance
  gates pass.
- Update public API comments and the variant catalogue with the final symbols.
- Add the new test command to the World/render row in `BUILD_AND_RUN.md`.

## Tests and acceptance criteria

Add `3rd/toridraw/toridraw_raster_kernel_test.c` and a `test-raster-kernel`
target in [`src/makefile`](../src/makefile). Because ToriDraw is unity-built,
any new implementation source must also be included by
[`toridraw_unity.c`](../3rd/toridraw/toridraw_unity.c) or an existing included
unit.

The routing test should use a spy kernel and synthetic model faces to prove:

- every drawable semantic class reaches exactly its matching slot once;
- `face_infos` absent and raw types 0/1/2/3/out-of-range combine correctly with
  normal/flat/hidden C shades;
- alpha absent/0/128/253/254/255 combines correctly with texture id
  -1/resident/missing, including the lighting-stage 254/255 effects and ignored
  ordinary stock textured alpha;
- hidden, invalid, alpha-skipped, unresolved-texture, back-facing, and
  unavailable-near-clip faces call no slot;
- flat callbacks receive A/A/A, effective opacity has one documented meaning,
  and prepared vertex, gate, texture, mapping, and source-face fields are exact;
- texture coordinates absent/-1/in-range and render types 0/1/2/3/malformed
  produce the correct payload tag; an explicit out-of-range coordinate is a
  named malformed-input skip, and non-vertex mapping values are never published
  as P/M/N indices;
- affine selection remains `texture_affine || render_type != 0`, including the
  coordinate-fallback edge case, and opaque/keyed selection is preserved;
- z-buffered flat/Gouraud/textured shade triples and framebuffer/z-buffer clip
  rebasing are correct when clip-left/top are nonzero and stride differs from
  width;
- all four classes cover their sorted, forced-zbuffer, and near-clip routes;
  sorted and unsorted culling use the same near-sentinel exemption;
- an untextured near-clipped fixture differing only in
  `textured_face_count` preserves the model-level scratch-availability quirk;
- normal stock rendering of a `MODEL_HD` uses only its base policy;
- texture-cache hit/miss behavior and residency changes between model passes do
  not leak stale prepared state;
- a sparse flat override inherits the other three slots from the stock or HD
  entry-point terminal as appropriate;
- two fallback layers retain the `user_data` of the supplying node;
- an explicit no-op suppresses a class while a null slot inherits it;
- invalid domains and fallback cycles are rejected through the validation
  status path without relying on a platform-specific death test;
- incompatible-domain nodes are skipped, two scenes select different complete
  roots, reset follows the terminal, and process-default changes affect only
  unpinned chains at the next pass;
- set/reset from inside a callback is rejected and the borrowed chain remains
  live for the entire pass;
- Pixel16 routes every face surviving its common skip policy only to
  flat/Gouraud, never reaches its textured assertion stubs, and returns the
  defined unsupported result from HD entry points.

Run the new suite alongside:

- `make -C src test-scanline`
- `make -C src test-zbuffer`
- `make -C src test-near-clip`
- `make -C src test-model-render-flags`
- `make -C src test-render-hd`
- `make -C src test-pick`, which covers hidden and signed-alpha semantics
- a Pixel16 build of `test-raster-kernel`
- a normal optimized client/ToriDraw build

Extend near-clip coverage to include flat and both textured stock classes. In
the HD phases, cover raw/malformed mapping types, all gates, missing materials
and both the in-range/invalid-PMN missing-mapping outcomes, both solid variants,
depth mode, and the new near-plane rebuild. Add explicit no-callback coverage
for null lit-colour arrays, invalid-width material fallback, and invalid-gate
coercion. The modulation test must compare pixels from an authored HSL hue that
differs from the lit 0..127 shade; routing counters alone cannot catch use of the
wrong colour source.

The functional interface cutover is complete when:

1. Complete branching and scanline roots match their stock oracles, including
   the documented scanline differences.
2. The Pixel16 stock oracle is unchanged; forced z-buffer retains its assertion
   and HD returns its defined unsupported result.
3. Every accepted stock face is either skipped for its documented reason or
   invokes exactly one resolved public slot.
4. Sparse/domain-filtered overrides work without per-face chain traversal, and
   two scenes can use different variants independently.
5. Existing scanline, z-buffer, near-clip, model-flag, picking, and HD tests
   continue to pass. HD near-plane reconstruction remains Phase 6 and is not
   claimed as part of the completed adapter.
6. Optimized stock builds contain no runtime family read in or below the
   selected callback; HD contains only its documented private table call.

The remaining workload-level release gate is for the complete and sparse paths
to meet the performance threshold across the stated small, mixed, and large
fixtures. Optimized-output inspection already confirms that stock family
selection is outside the callbacks and their descendants; the broader timed
fixture run is intentionally tracked separately from functional acceptance.

## Expected file changes

| File | Change |
|---|---|
| `3rd/toridraw/toridraw_texture_mapping.h` | public prepared HD mapping representation |
| `3rd/toridraw/toridraw_raster_kernel.h` | public object, vtable, descriptors, built-in getters |
| `3rd/toridraw/toridraw_raster_kernel.c` | chain validation/resolution and scene binding APIs |
| `3rd/toridraw/toridraw.h` | export the new API; deprecate global scanline selection |
| `3rd/toridraw/toridraw_types.h` | public mapping include plus nullable scene binding and active flag |
| `3rd/toridraw/toridraw.c` | Pixel16 guards and same-scene recursive-render rejection |
| `3rd/toridraw/toridraw_raster.u.c` | classifier/preparer, domain-aware chain resolution, one-call face dispatch |
| `3rd/toridraw/triangles/*` | direct branching/scanline face callbacks; remove leaf global checks |
| `3rd/toridraw/toridraw_render_hd.u.c` | HD domain roots, prepared policy adapter, later near-clip closure |
| `3rd/toridraw/toridraw_raster_kernel_test.c` | routing, override, lifetime, and safety tests |
| `3rd/toridraw/toridraw_unity.c` | include any new implementation unit |
| `src/makefile` | test target and relevant build wiring |
| `BUILD_AND_RUN.md` | advertise the new World/render test command |

This layout keeps the public interface small, the face policy centralized, and
the expensive variant matrix inside concrete implementations where it can be
optimized without changing user code.
