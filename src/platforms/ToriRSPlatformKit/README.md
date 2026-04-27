# ToriRSPlatformKit (TRSPK)

TRSPK is a modular C graphics toolkit for building concrete ToriRS platform renderers. It is a toolkit, not a framework: there is no central command dispatcher, no virtual interface, no tagged backend union, and no hash table for model lookups. Platform code owns its `TORIRS_GFX_*` switch statement and calls the specific backend/tools it needs.

## Folder Structure

```text
platforms/ToriRSPlatformKit/
├── README.md
├── include/ToriRSPlatformKit/
│   ├── trspk_types.h
│   └── trspk_math.h
└── src/
    ├── tools/
    │   ├── trspk_resource_cache.h / .c
    │   ├── trspk_pass_batch16.h / .c
    │   ├── trspk_pass_batch32.h / .c
    │   ├── trspk_cache_model_loader16.h / .c
    │   └── trspk_cache_model_loader32.h / .c
    └── backends/
        ├── metal/
        │   ├── trspk_metal.h
        │   ├── metal_core.m
        │   ├── metal_3d_cache.m
        │   └── metal_draw.m
        └── webgl1/
            ├── trspk_webgl1.h
            ├── webgl1_core.c
            ├── webgl1_3d_cache.c
            └── webgl1_draw.c
```

## File Purposes

`trspk_types.h` defines shared data types: fixed IDs, `TRSPK_Vertex`, `TRSPK_ModelPose`, `TRSPK_BatchResource`, atlas tiles, bake transforms, and batch entries.

`trspk_math.h` provides inline math used while loading and drawing: matrix helpers, camera matrices, HSL16 color conversion, bake transform application, 64-to-128 RGBA upscaling, and PNM UV projection.

`trspk_resource_cache.*` is the CPU metadata store. It uses fixed arrays indexed by model ID, texture ID, and batch ID. It tracks model poses, animation offsets, scene batch resources, bake transforms, atlas pixels, and atlas tile metadata. It does not own backend GPU lifetime policy beyond returning handles to the backend.

`trspk_pass_batch16.*` is a 16-bit batcher for WebGL1. It automatically creates chunks so each chunk stays within 65535 vertices.

`trspk_pass_batch32.*` is a 32-bit continuous batcher for Metal. It appends all model geometry into one vertex/index stream.

`trspk_cache_model_loader16.*` and `trspk_cache_model_loader32.*` expand raw model arrays into `TRSPK_Vertex` streams. They follow the old `BatchAddModeli16` and `BatchAddModelTexturedi16` input pattern: vertex coordinate arrays, face index arrays, HSL16 colors, optional alpha/info arrays, optional texture face arrays, UV mode, and optional bake transform.

`metal_core.m` owns Metal setup: device, command queue, frame semaphore, ring buffers, drawable acquisition, and frame begin/end.

`metal_3d_cache.m` uploads atlas data and merged batch geometry into Metal buffers/textures, then records poses in the resource cache.

`metal_draw.m` accumulates sorted model indices and submits them with one `drawIndexedPrimitives` call per 3D pass.

`webgl1_core.c` owns WebGL1 context/program setup, atlas initialization, dynamic index buffer creation, and frame begin/end.

`webgl1_3d_cache.c` uploads atlas data and chunked batch geometry into GL buffers/textures, then records poses in the resource cache.

`webgl1_draw.c` accumulates sorted indices per 65k chunk and submits one `glDrawElements` per chunk that had draws.

## How To Use

Initialize exactly one backend:

```c
TRSPK_MetalRenderer* metal = TRSPK_Metal_Init(ca_metal_layer, width, height);
TRSPK_WebGL1Renderer* webgl = TRSPK_WebGL1_Init("#canvas", width, height);
```

Write your own command switch. TRSPK intentionally does not define command types:

```c
TRSPK_Metal_FrameBegin(renderer);
TRSPK_ResourceCache* cache = TRSPK_Metal_GetCache(renderer);

while (LibToriRS_FrameNextCommand(game, commands, &cmd, true)) {
    switch (cmd.kind) {
    case TORIRS_GFX_BATCH3D_BEGIN:
        trspk_batch32_begin(TRSPK_Metal_GetBatchStaging(renderer));
        trspk_resource_cache_batch_begin(cache, cmd._batch3d_begin.batch_id);
        break;
    case TORIRS_GFX_BATCH3D_MODEL_ADD:
        trspk_batch32_add_model_textured(TRSPK_Metal_GetBatchStaging(renderer),
            model_id, TRSPK_GPU_ANIM_NONE_IDX, 0, vertex_count,
            vx, vy, vz, face_count, fa, fb, fc, ca, cb, cc,
            textures, textured_faces, tfa, tfb, tfc, alphas, infos,
            TRSPK_UV_MODE_TEXTURED_FACE_ARRAY, bake);
        break;
    case TORIRS_GFX_BATCH3D_END:
        trspk_metal_cache_batch_submit(renderer, cmd._batch3d_end.batch_id, usage_hint);
        break;
    case TORIRS_GFX_DRAW_MODEL: {
        const TRSPK_ModelPose* pose = trspk_resource_cache_get_pose_for_draw(
            cache, cmd._model_draw.model_id, cmd._model_draw.use_animation,
            cmd._model_draw.anim_index, cmd._model_draw.frame_index);
        if (pose) {
            trspk_metal_draw_add_model(renderer, pose, sorted_indices, index_count);
        }
        break;
    }
    case TORIRS_GFX_STATE_END_3D:
        trspk_metal_draw_submit_3d(renderer, &view, &projection);
        break;
    }
}

TRSPK_Metal_FrameEnd(renderer);
```

## Loading vs Rendering

During loading (`BATCH3D_BEGIN` through `BATCH3D_END`), raw models and animation frames are baked into VBO/EBO data. Metal uses `TRSPK_Batch32`, creating a single merged VBO/EBO. WebGL1 uses `TRSPK_Batch16`, creating one VBO/EBO pair for each 65k-safe chunk. Each uploaded model or animation frame records a `TRSPK_ModelPose` in the resource cache.

During rendering (`DRAW_MODEL` between `BEGIN_3D` and `END_3D`), platform code looks up a pose with:

```c
trspk_resource_cache_get_pose_for_draw(cache, model_id, use_anim, anim_index, frame_index);
```

Metal then appends all visible scenery indices to a single dynamic `uint32_t` pool and submits one `drawIndexedPrimitives`. WebGL1 appends visible indices to the pool for `pose->chunk_index` and submits one `glDrawElements` per chunk with draws.

## Texture Atlas

Textures are packed into a 2048x2048 atlas made from 128x128 tiles. Texture ID `N` maps directly to atlas cell `N`, so vertices only need to carry `tex_id`. `trspk_resource_cache_load_texture_128` copies RGBA pixels into the CPU atlas and updates `TRSPK_AtlasTile`.

Metal uploads the CPU atlas to one `MTLTexture` and stores the 256 `TRSPK_AtlasTile` records in an `MTLBuffer`.

WebGL1 uploads the CPU atlas to one `GL_TEXTURE_2D`.

## Why WebGL1 Uses Tile Uniforms

WebGL1/GLES2 cannot read arbitrary shader storage buffers and does not have texture arrays in the baseline profile. Metal can bind a tile metadata buffer and read `tiles[tex_id]`; WebGL1 cannot.

The WebGL1 backend therefore packs tile metadata into two uniform arrays:

```glsl
uniform vec4 u_tileA[256]; // u0, v0, du, dv
uniform vec4 u_tileB[256]; // anim_u, anim_v, opaque, pad
```

The fragment shader uses `int(v_tex_id)` to select the tile. The backend tracks `tiles_dirty` and only calls `glUniform4fv` after texture metadata changes.

## Metal Uniform Ring Buffer

Metal uses a uniform ring buffer so the CPU does not overwrite uniform bytes that the GPU may still be reading. The ring has `TRSPK_METAL_INFLIGHT_FRAMES` frame slots. Each frame slot contains `TRSPK_METAL_MAX_3D_PASSES_PER_FRAME` pass slots, because a frame can contain multiple `BEGIN_3D`/`END_3D` pairs.

The uniform offset is:

```c
offset = frame_slot * (max_passes * aligned_uniform_size)
       + pass_subslot * aligned_uniform_size;
```

The dynamic index buffer uses a similar per-pass upload offset. WebGL1 uses immediate `glUniform*` calls instead, so it does not need a uniform ring buffer.

## Constraints

- Model lookup is array-based: `models[model_id]`
- Texture lookup is array-based: `atlas_tiles[texture_id]`
- Batch lookup is array-based: `batches[batch_id]`
- Max models: 32768
- Max textures: 256
- Max batches: 10
- WebGL1 chunk limit: 65535 vertices
- Atlas size: 2048x2048
- Tile size: 128x128

## Animation

The resource cache tracks bind, primary animation, and secondary animation segments:

- `TRSPK_GPU_ANIM_NONE_IDX`
- `TRSPK_GPU_ANIM_PRIMARY_IDX`
- `TRSPK_GPU_ANIM_SECONDARY_IDX`

Use `trspk_resource_cache_allocate_pose_slot` while loading animation frames, and `trspk_resource_cache_get_pose_for_draw` when a draw command arrives.
