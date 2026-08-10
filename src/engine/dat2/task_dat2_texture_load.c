#include "engine/dat2/dat2_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/proctex/proctex_generator.h"
#include "engine/texture_palette_bake.h"
#include "engine/torirs_types.h"

#include "datatypes/dat2_proctexture.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <rscache.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct TextureLayer
{
    const uint8_t* palette_pixels;
    int width;
    int height;
    const int* palette; /* RGB entries */
    int palette_length;
    /* Per-pixel coverage from the sprite's alpha plane (dat2 FLAG_ALPHA), used
     * only by an alpha-blended texture. NULL when the sprite has none, which is
     * every stock texture sprite. */
    const uint8_t* pixel_alphas;
    int blend_type; /* from dat2 sprite_types; 0 = replace */
};

struct Task_Dat2TextureLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int texture_id;
    struct RSCache_Dat2Texture* def;
    struct RSCache_Dat2SpritePack** packs;
    int sprite_index;
    struct RSCache_Dat2ProcTexture* proc_def;

    /* Procedural dependency closure, walked one item per await. Both are worklists that grow
     * while being walked: resolving a texture dependency can introduce further texture and
     * sprite dependencies of its own. */
    int dep_textures[64];
    int dep_texture_count;
    int dep_texture_cursor;
    int dep_sprites[64];
    int dep_sprite_count;
    int dep_sprite_cursor;
    /*
     * The id currently being awaited, per worklist.
     *
     * These MUST be task state, not locals. A PT_YIELD resumes at the yield statement with the
     * frame's locals gone, so an id read before the await is garbage after it — and the garbage
     * was silently used as the key the decoded program got stored under, so every later lookup
     * missed and texture_source/sprite_source refused. Same gotcha the loop cursors above exist
     * for; it just bites harder here because the corrupted value is a cache key rather than an
     * index that would trip a bounds check.
     */
    int dep_texture_id;
    int dep_sprite_id;
};

/* Size procedural textures are baked at. The material's `small` flag selects 64 in the
 * reference; we bake everything at 128 to match what the sprite-backed path already does, so
 * the scene texture map stays homogeneous. */
#define PROCTEX_BAKE_SIZE 128

/* --- dependency caches ----------------------------------------------------------------- */

static struct RSCache_Dat2ProcTexture*
proctex_program_get(
    struct Dat2BuildCache* bc,
    int texture_id)
{
    if( !bc->proctex_programs || texture_id < 0 || texture_id >= bc->proctex_program_capacity )
        return NULL;
    return bc->proctex_programs[texture_id];
}

static void
proctex_program_put(
    struct Dat2BuildCache* bc,
    int texture_id,
    struct RSCache_Dat2ProcTexture* program)
{
    if( texture_id < 0 || texture_id > 0xFFFF )
    {
        RSCache_Dat2ProcTextureFree(program);
        return;
    }
    if( texture_id >= bc->proctex_program_capacity )
    {
        int want = texture_id + 64;
        struct RSCache_Dat2ProcTexture** grown =
            realloc(bc->proctex_programs, (size_t)want * sizeof(*grown));
        if( !grown )
        {
            RSCache_Dat2ProcTextureFree(program);
            return;
        }
        memset(
            grown + bc->proctex_program_capacity,
            0,
            (size_t)(want - bc->proctex_program_capacity) * sizeof(*grown));
        bc->proctex_programs = grown;
        bc->proctex_program_capacity = want;
    }
    if( bc->proctex_programs[texture_id] )
        RSCache_Dat2ProcTextureFree(bc->proctex_programs[texture_id]);
    bc->proctex_programs[texture_id] = program;
}

static struct Dat2ProcTexSprite*
proctex_sprite_get(
    struct Dat2BuildCache* bc,
    int sprite_id)
{
    for( int i = 0; i < bc->proctex_sprite_count; i++ )
        if( bc->proctex_sprites[i].sprite_id == sprite_id )
            return &bc->proctex_sprites[i];
    return NULL;
}

static void
proctex_sprite_put(
    struct Dat2BuildCache* bc,
    int sprite_id,
    int32_t* argb,
    int width,
    int height)
{
    if( bc->proctex_sprite_count == bc->proctex_sprite_capacity )
    {
        int want = bc->proctex_sprite_capacity ? bc->proctex_sprite_capacity * 2 : 32;
        struct Dat2ProcTexSprite* grown =
            realloc(bc->proctex_sprites, (size_t)want * sizeof(*grown));
        if( !grown )
        {
            free(argb);
            return;
        }
        bc->proctex_sprites = grown;
        bc->proctex_sprite_capacity = want;
    }
    bc->proctex_sprites[bc->proctex_sprite_count].sprite_id = sprite_id;
    bc->proctex_sprites[bc->proctex_sprite_count].argb = argb;
    bc->proctex_sprites[bc->proctex_sprite_count].width = width;
    bc->proctex_sprites[bc->proctex_sprite_count].height = height;
    bc->proctex_sprite_count++;
}

/* --- generator resolvers ---------------------------------------------------------------- */

/*
 * Both resolvers read only from the caches above, which the task has already populated by
 * awaiting the dependency closure. They never trigger IO: the generator runs synchronously
 * inside one protothread step, so a blocking load here is not expressible.
 */
static bool
proctex_resolve_sprite(
    void* user,
    int sprite_id,
    const int32_t** out_argb,
    int* out_width,
    int* out_height)
{
    struct Dat2BuildCache* bc = user;
    struct Dat2ProcTexSprite* sprite = proctex_sprite_get(bc, sprite_id);

    if( !sprite || !sprite->argb )
        return false;
    *out_argb = sprite->argb;
    *out_width = sprite->width;
    *out_height = sprite->height;
    return true;
}

static struct ToriRS_Texture*
proctex_bake(
    const struct RSCache_Dat2ProcTexture* def,
    const struct RSCache_Dat2MaterialTable* materials,
    struct Dat2BuildCache* bc,
    int texture_id,
    int size);

/*
 * A nested texture source. Renders the dependency's own program on demand and memoises the
 * ARGB result on the buildcache as a pseudo-sprite, so a texture referenced by several others
 * is evaluated once.
 *
 * Brightness is **1.0**, not the 0.8 used for the final image: a nested texture is an
 * intermediate signal feeding more operations, and gamma-correcting it here would apply the
 * curve twice. The reference passes 1.0 for the same reason.
 */
static bool
proctex_resolve_texture(
    void* user,
    int texture_id,
    int size,
    const int32_t** out_argb)
{
    struct Dat2BuildCache* bc = user;
    /* Keyed in the sprite cache under a namespace that cannot collide with a real sprite id. */
    int cache_key = 0x1000000 + (texture_id << 8) + (size & 0xFF);
    struct Dat2ProcTexSprite* cached = proctex_sprite_get(bc, cache_key);
    struct RSCache_Dat2ProcTexture* program;
    struct ProcTexGenerator* gen;
    int32_t* pixels;
    int unsupported = 0;

    if( cached )
    {
        if( !cached->argb )
            return false;
        *out_argb = cached->argb;
        return true;
    }

    program = proctex_program_get(bc, texture_id);
    if( !program || !ProcTexGenerator_IsFullySupported(program, NULL) )
    {
        /* Memoise the failure too, so a broken dependency is not retried per scanline. */
        proctex_sprite_put(bc, cache_key, NULL, 0, 0);
        return false;
    }

    pixels = calloc((size_t)size * (size_t)size, sizeof(*pixels));
    if( !pixels )
        return false;

    gen = ProcTexGenerator_New(proctex_resolve_sprite, proctex_resolve_texture, bc);
    if( !gen )
    {
        free(pixels);
        return false;
    }
    if( !ProcTexGenerator_Render(gen, program, size, 1.0, pixels, &unsupported, NULL) ||
        unsupported > 0 )
    {
        ProcTexGenerator_Free(gen);
        free(pixels);
        proctex_sprite_put(bc, cache_key, NULL, 0, 0);
        return false;
    }
    ProcTexGenerator_Free(gen);

    proctex_sprite_put(bc, cache_key, pixels, size, size);
    *out_argb = pixels;
    return true;
}

/* Append to a worklist, skipping ids already queued. Returns false when full — bounded so a
 * pathological dependency graph cannot grow the walk without limit. */
static bool
proctex_dep_queue(
    int* list,
    int* count,
    int capacity,
    int value)
{
    if( value < 0 )
        return true;
    for( int i = 0; i < *count; i++ )
        if( list[i] == value )
            return true;
    if( *count >= capacity )
        return false;
    list[(*count)++] = value;
    return true;
}

/*
 * Evaluate one procedural texture into a ToriRS_Texture.
 *
 * Refuses the texture when any operation in its graph has no ported evaluator. That is the
 * whole point of the check: an unported operation contributes flat mid-grey, which would
 * composite into something that looks like a real texture and is not. A missing texture makes
 * the face fall back to flat shading, which is visibly "not textured yet" rather than
 * confidently wrong.
 */
static struct ToriRS_Texture*
proctex_bake(
    const struct RSCache_Dat2ProcTexture* def,
    const struct RSCache_Dat2MaterialTable* materials,
    struct Dat2BuildCache* bc,
    int texture_id,
    int size)
{
    struct ProcTexGenerator* gen;
    struct ToriRS_Texture* texture;
    int32_t* pixels;
    int unsupported = 0;
    bool transparent = false;
    int first_unsupported = -1;

    if( !ProcTexGenerator_IsFullySupported(def, &first_unsupported) )
    {
        if( getenv("TORIRS_PROCTEX_DEBUG") )
            fprintf(
                stderr,
                "proctex %d: skipped, operation %d (%s) has no evaluator yet\n",
                texture_id,
                first_unsupported,
                RSCache_ProcTexOpName(first_unsupported));
        return NULL;
    }

    pixels = calloc((size_t)size * (size_t)size, sizeof(*pixels));
    if( !pixels )
        return NULL;

    gen = ProcTexGenerator_New(proctex_resolve_sprite, proctex_resolve_texture, bc);
    if( !gen )
    {
        free(pixels);
        return NULL;
    }

    /* 0.8 matches the gamma the sprite-backed path applies via ToriRS_TextureGammaBlend, so
     * the two systems land at a comparable brightness in the same scene. */
    if( !ProcTexGenerator_Render(gen, def, size, 0.8, pixels, &unsupported, &transparent) ||
        unsupported > 0 )
    {
        if( getenv("TORIRS_PROCTEX_DEBUG") )
            fprintf(stderr, "proctex %d: render failed (unsupported=%d)\n",
                    texture_id, unsupported);
        ProcTexGenerator_Free(gen);
        free(pixels);
        return NULL;
    }
    ProcTexGenerator_Free(gen);

    texture = calloc(1, sizeof(*texture));
    if( !texture )
    {
        free(pixels);
        return NULL;
    }
    texture->texels = pixels;
    texture->width = size;
    texture->height = size;
    texture->opaque = !transparent;
    /* Animation and average HSL come from the material when there is one — for rev 555+ the
     * material's animation wins over the texture record's own copy. */
    if( materials && texture_id >= 0 && texture_id < materials->count )
    {
        texture->animation_direction = materials->materials[texture_id].anim_u;
        texture->animation_speed = materials->materials[texture_id].anim_v;
        texture->average_hsl = materials->materials[texture_id].average_hsl;
    }
    else
    {
        texture->animation_direction = def->anim_u;
        texture->animation_speed = def->anim_v;
    }
    if( texture->average_hsl == 0 )
        texture->average_hsl = ToriRS_TextureAverageHsl(pixels, size, size);

    return texture;
}

static void
task_dat2_texture_load_clear_packs(struct Task_Dat2TextureLoad* task)
{
    int i;

    assert(task);

    if( task->packs && task->def )
    {
        for( i = 0; i < task->def->sprite_ids_count; i++ )
        {
            if( task->packs[i] )
                RSCache_Dat2SpritePackFree(task->packs[i]);
        }
    }
    free(task->packs);
    task->packs = NULL;
    task->def = NULL;
    task->sprite_index = 0;
}


/** Replace a texel's alpha with the sprite's per-pixel coverage.
 *  The palette supplies colour only: indices are shared between pixels, so two
 *  pixels of the same colour would otherwise be forced to the same coverage. */
static inline int
texture_apply_pixel_alpha(int texel, const uint8_t* pixel_alphas, int index)
{
    if( !pixel_alphas )
        return texel;
    return (int)(((uint32_t)pixel_alphas[index] << 24) | ((uint32_t)texel & 0x00FFFFFFu));
}

static struct ToriRS_Texture*
texture_bake(
    const struct TextureLayer* layers,
    int layer_count,
    int dest_size,
    int animation_direction,
    int animation_speed,
    int average_hsl,
    bool alpha_blended)
{
    struct ToriRS_Texture* texture;
    int* pixels;
    bool opaque;
    int i;

    assert(layers);
    assert(layer_count > 0);
    assert(dest_size == 64 || dest_size == 128);

    pixels = calloc((size_t)dest_size * (size_t)dest_size, sizeof(*pixels));
    if( !pixels )
        return NULL;

    opaque = true;

    for( i = 0; i < layer_count; i++ )
    {
        const struct TextureLayer* layer = &layers[i];
        int* adjusted_palette;
        int pi;
        int pixel_index;

        assert(layer->palette_pixels);
        assert(layer->palette);
        assert(layer->palette_length > 0);
        assert(layer->width > 0 && layer->height > 0);

        adjusted_palette = malloc((size_t)layer->palette_length * sizeof(*adjusted_palette));
        if( !adjusted_palette )
        {
            free(pixels);
            return NULL;
        }

        for( pi = 0; pi < layer->palette_length; pi++ )
        {
            int alpha = 0xff;
            if( (layer->palette[pi] & 0xf8f8ff) == 0 )
                alpha = 0;
            adjusted_palette[pi] = (alpha << 24) | ToriRS_TextureGammaBlend(layer->palette[pi], 0.8);
        }

        for( pixel_index = 0; pixel_index < layer->width * layer->height; pixel_index++ )
        {
            int palette_index = layer->palette_pixels[pixel_index];
            assert(palette_index >= 0 && palette_index < layer->palette_length);
            if( (adjusted_palette[palette_index] & 0xf8f8ff) == 0 )
                opaque = false;
        }

        /* An alpha-blended texture takes its coverage from the sprite's alpha
         * plane, per pixel. The palette cannot carry it: indices are shared, so
         * two pixels of the same colour would be forced to the same coverage. */
        if( alpha_blended && !layer->pixel_alphas )
            alpha_blended = false;

        if( getenv("TORIRS_TEX_DEBUG") )
            fprintf(
                stderr,
                "tex_bake: layer=%d blend=%d w=%d h=%d palette_len=%d dest=%d\n",
                i,
                layer->blend_type,
                layer->width,
                layer->height,
                layer->palette_length,
                dest_size);

        /* Only blend_type 0 (replace) is implemented, matching legacy decode. */
        if( layer->blend_type == 0 )
        {
            if( dest_size == layer->width )
            {
                for( pixel_index = 0; pixel_index < layer->width * layer->height; pixel_index++ )
                {
                    int palette_index = layer->palette_pixels[pixel_index];
                    pixels[pixel_index] = alpha_blended
                                              ? texture_apply_pixel_alpha(
                                                    adjusted_palette[palette_index],
                                                    layer->pixel_alphas, pixel_index)
                                              : adjusted_palette[palette_index];
                }
            }
            else if( layer->width == 64 && dest_size == 128 )
            {
                int x;
                int y;
                pixel_index = 0;
                for( x = 0; x < dest_size; x++ )
                {
                    for( y = 0; y < dest_size; y++ )
                    {
                        int src_index = ((x >> 1) << 6) + (y >> 1);
                        int palette_index = layer->palette_pixels[src_index];
                        pixels[pixel_index++] =
                            alpha_blended ? texture_apply_pixel_alpha(
                                                adjusted_palette[palette_index],
                                                layer->pixel_alphas, src_index)
                                          : adjusted_palette[palette_index];
                    }
                }
            }
            else if( layer->width == 128 && dest_size == 64 )
            {
                int x;
                int y;
                pixel_index = 0;
                for( x = 0; x < dest_size; x++ )
                {
                    for( y = 0; y < dest_size; y++ )
                    {
                        int src_index = (y << 1) + ((x << 1) << 7);
                        int palette_index = layer->palette_pixels[src_index];
                        pixels[pixel_index++] =
                            alpha_blended ? texture_apply_pixel_alpha(
                                                adjusted_palette[palette_index],
                                                layer->pixel_alphas, src_index)
                                          : adjusted_palette[palette_index];
                    }
                }
            }
        }

        free(adjusted_palette);
    }

    texture = calloc(1, sizeof(*texture));
    if( !texture )
    {
        free(pixels);
        return NULL;
    }

    texture->texels = pixels;
    texture->width = dest_size;
    texture->height = dest_size;
    texture->opaque = opaque && !alpha_blended;
    texture->alpha_blended = alpha_blended;
    texture->animation_direction = animation_direction;
    texture->animation_speed = animation_speed;
    texture->average_hsl = average_hsl;
    if( texture->average_hsl == 0 )
        texture->average_hsl = ToriRS_TextureAverageHsl(pixels, dest_size, dest_size);

    return texture;
}

static struct ToriRS_Texture*
texture_from_sprite_packs(
    struct RSCache_Dat2SpritePack* const* packs,
    const struct RSCache_Dat2Texture* def,
    int dest_size)
{
    struct TextureLayer* layers;
    struct ToriRS_Texture* texture;
    int i;

    assert(packs);
    assert(def);
    assert(def->sprite_ids_count > 0);
    assert(dest_size == 64 || dest_size == 128);

    layers = calloc((size_t)def->sprite_ids_count, sizeof(*layers));
    if( !layers )
        return NULL;

    for( i = 0; i < def->sprite_ids_count; i++ )
    {
        struct RSCache_Dat2SpritePack* pack = packs[i];
        struct RSCache_Dat2Sprite* sprite;

        assert(pack);
        assert(pack->count > 0);
        sprite = &pack->sprites[0];

        layers[i].palette_pixels = sprite->palette_pixels;
        layers[i].width = sprite->width;
        layers[i].height = sprite->height;
        layers[i].palette = pack->palette;
        layers[i].palette_length = pack->palette_length;
        layers[i].pixel_alphas = sprite->pixel_alphas;
        layers[i].blend_type = 0;
        if( i > 0 && def->sprite_types )
            layers[i].blend_type = def->sprite_types[i - 1];
    }

    texture = texture_bake(
        layers,
        def->sprite_ids_count,
        dest_size,
        def->animation_direction,
        def->animation_speed,
        def->average_hsl,
        def->alpha_blended);
    free(layers);
    return texture;
}

static int
Task_Dat2TextureLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2TextureLoad* task = (struct Task_Dat2TextureLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct ToriRS_Texture* torirs_texture = NULL;

    PT_BEGIN(&task->pt);

    /*
     * Decide which texture system this cache uses, once. The materials table existing is the
     * gate (see Dat2BuildCache.proctex_mode), so probe table 26 rather than infer from the
     * revision — a cache can be new enough and still not ship one.
     */
    if( task->bc->proctex_mode == DAT2_PROCTEX_UNPROBED )
    {
        if( RSCache_Dat2UsesProcTextures(CacheProvider_Profile(&task->bc->base), true) )
        {
            RSCache_IO_Dat2MaterialTableLoad(io, 0);
            PT_YIELD(&task->pt);
            archive = RSCache_IO_Dat2MaterialTableDecode(io, 0);
            if( archive )
            {
                task->bc->materials = RSCache_Dat2MaterialTableNewDecode(
                    archive->data,
                    archive->data_size,
                    RSCache_Dat2ProcTextureFlags(CacheProvider_Profile(&task->bc->base)));
                RSCache_Dat2DiskArchiveFree(archive);
                archive = NULL;
            }
            task->bc->proctex_mode =
                task->bc->materials ? DAT2_PROCTEX_PROCEDURAL : DAT2_PROCTEX_SPRITE;
        }
        else
        {
            task->bc->proctex_mode = DAT2_PROCTEX_SPRITE;
        }

        fprintf(
            stderr,
            "textures: %s system%s\n",
            task->bc->proctex_mode == DAT2_PROCTEX_PROCEDURAL ? "procedural (RS2 materials)"
                                                              : "sprite-backed",
            task->bc->materials ? "" : " (no materials table)");
    }

    if( task->bc->proctex_mode == DAT2_PROCTEX_PROCEDURAL )
    {
        struct ToriRS_Texture* proc_texture = NULL;

        /* RS2 gives every texture id its own archive, so there is no shared group to memoise
         * — one load per id. */
        RSCache_IO_Dat2ProcTextureLoad(io, 0, task->texture_id);
        PT_YIELD(&task->pt);
        archive = RSCache_IO_Dat2ProcTextureDecode(io, 0);
        if( !archive )
        {
            fprintf(stderr, "Failed to load proc texture %d\n", task->texture_id);
            PT_EXIT(&task->pt);
        }

        task->proc_def = RSCache_Dat2ProcTextureNewDecode(
            archive->data,
            archive->data_size,
            task->texture_id,
            RSCache_Dat2ProcTextureFlags(CacheProvider_Profile(&task->bc->base)));
        RSCache_Dat2DiskArchiveFree(archive);
        archive = NULL;

        if( !task->proc_def )
        {
            fprintf(stderr, "Failed to decode proc texture %d\n", task->texture_id);
            PT_EXIT(&task->pt);
        }
        proctex_program_put(task->bc, task->texture_id, task->proc_def);
        /* Owned by the buildcache from here; the local is only a decode handle. */
        task->proc_def = NULL;

        /*
         * Walk the dependency closure before baking. `texture_source` names other programs and
         * `sprite_source` names sprites, and the generator runs synchronously inside a single
         * protothread step — so nothing can be fetched once baking starts. Everything the graph
         * can reach has to be resident first.
         *
         * The worklist grows while being walked (a dependency has dependencies), and residency
         * is what terminates it: an already-decoded program is never re-queued, so a cyclic
         * texture_source reference is naturally bounded.
         */
        proctex_dep_queue(
            task->dep_textures, &task->dep_texture_count, 64, task->texture_id);
        for( task->dep_texture_cursor = 0;
             task->dep_texture_cursor < task->dep_texture_count;
             task->dep_texture_cursor++ )
        {
            struct RSCache_Dat2ProcTexture* dep;

            task->dep_texture_id = task->dep_textures[task->dep_texture_cursor];
            if( !proctex_program_get(task->bc, task->dep_texture_id) )
            {
                RSCache_IO_Dat2ProcTextureLoad(io, 0, task->dep_texture_id);
                PT_YIELD(&task->pt);
                archive = RSCache_IO_Dat2ProcTextureDecode(io, 0);
                if( !archive )
                    continue;
                dep = RSCache_Dat2ProcTextureNewDecode(
                    archive->data,
                    archive->data_size,
                    task->dep_texture_id,
                    RSCache_Dat2ProcTextureFlags(CacheProvider_Profile(&task->bc->base)));
                RSCache_Dat2DiskArchiveFree(archive);
                archive = NULL;
                if( !dep )
                    continue;
                proctex_program_put(task->bc, task->dep_texture_id, dep);
            }

            /* Re-read through the cache rather than reusing a pointer from before the await. */
            dep = proctex_program_get(task->bc, task->dep_texture_id);
            if( !dep )
                continue;

            for( int i = 0; i < dep->texture_dependency_count; i++ )
                proctex_dep_queue(
                    task->dep_textures,
                    &task->dep_texture_count,
                    64,
                    dep->texture_dependencies[i]);
            for( int i = 0; i < dep->sprite_dependency_count; i++ )
                proctex_dep_queue(
                    task->dep_sprites,
                    &task->dep_sprite_count,
                    64,
                    dep->sprite_dependencies[i]);
        }

        /* Sprite dependencies: decode each pack once and flatten to ARGB for the generator. */
        for( task->dep_sprite_cursor = 0;
             task->dep_sprite_cursor < task->dep_sprite_count;
             task->dep_sprite_cursor++ )
        {
            struct RSCache_Dat2SpritePack* pack;

            task->dep_sprite_id = task->dep_sprites[task->dep_sprite_cursor];
            if( proctex_sprite_get(task->bc, task->dep_sprite_id) )
                continue;

            RSCache_IO_Dat2SpriteLoad(io, 0, task->dep_sprite_id);
            PT_YIELD(&task->pt);
            archive = RSCache_IO_Dat2SpriteDecode(io, 0);
            if( !archive )
            {
                /* Record the miss so the resolver fails fast instead of re-searching. */
                proctex_sprite_put(task->bc, task->dep_sprite_id, NULL, 0, 0);
                continue;
            }

            pack = RSCache_Dat2SpritePackNewDecode(
                (const unsigned char*)archive->data,
                archive->data_size,
                RSCACHE_SPRITELOAD_FLAG_NORMALIZE);
            RSCache_Dat2DiskArchiveFree(archive);
            archive = NULL;

            if( pack && pack->count > 0 )
            {
                struct RSCache_Dat2Sprite* sprite = &pack->sprites[0];
                int count = sprite->width * sprite->height;
                int32_t* argb = malloc((size_t)(count > 0 ? count : 1) * sizeof(*argb));
                if( argb )
                {
                    for( int i = 0; i < count; i++ )
                        argb[i] = pack->palette[sprite->palette_pixels[i]];
                    proctex_sprite_put(
                        task->bc,
                        task->dep_sprite_id,
                        argb,
                        sprite->width,
                        sprite->height);
                }
            }
            else
            {
                proctex_sprite_put(task->bc, task->dep_sprite_id, NULL, 0, 0);
            }
            if( pack )
                RSCache_Dat2SpritePackFree(pack);
        }

        proc_texture = proctex_bake(
            proctex_program_get(task->bc, task->texture_id),
            task->bc->materials,
            task->bc,
            task->texture_id,
            PROCTEX_BAKE_SIZE);

        if( !proc_texture )
            PT_EXIT(&task->pt);

        CacheProvider_TextureAdd(&task->bc->base, task->texture_id, proc_texture);
        PT_EXIT(&task->pt);
    }

    /* All texture defs decode on first touch; later tasks skip the archive
     * load entirely instead of re-decompressing it per texture. */
    if( !dat2_buildcache_texture_get(task->bc, task->texture_id) )
    {
        RSCache_IO_Dat2TextureGroupLoad(io, 0);
        PT_YIELD(&task->pt);

        archive = RSCache_IO_Dat2TextureGroupDecode(io, 0);
        if( !archive )
        {
            fprintf(
                stderr, "Failed to decode dat2 texture group for texture %d\n", task->texture_id);
            PT_EXIT(&task->pt);
        }

        dat2_buildcache_textures_init_from_archive(task->bc, archive, NULL, 0);
        RSCache_Dat2DiskArchiveFree(archive);
    }

    task->def = dat2_buildcache_texture_get(task->bc, task->texture_id);
    if( !task->def )
    {
        fprintf(stderr, "Failed to load dat2 texture %d\n", task->texture_id);
        PT_EXIT(&task->pt);
    }

    if( task->def->sprite_ids_count <= 0 )
    {
        fprintf(stderr, "Dat2 texture %d has no sprite layers\n", task->texture_id);
        PT_EXIT(&task->pt);
    }

    task->packs = calloc((size_t)task->def->sprite_ids_count, sizeof(*task->packs));
    if( !task->packs )
    {
        fprintf(stderr, "Failed to allocate sprite packs for texture %d\n", task->texture_id);
        PT_EXIT(&task->pt);
    }
    task->sprite_index = 0;

    for( ; task->sprite_index < task->def->sprite_ids_count; task->sprite_index++ )
    {
        RSCache_IO_Dat2SpriteLoad(io, 0, task->def->sprite_ids[task->sprite_index]);
        PT_YIELD(&task->pt);

        archive = RSCache_IO_Dat2SpriteDecode(io, 0);
        if( !archive )
        {
            fprintf(
                stderr,
                "Failed to decode sprite %d for texture %d\n",
                task->def->sprite_ids[task->sprite_index],
                task->texture_id);
            task_dat2_texture_load_clear_packs(task);
            PT_EXIT(&task->pt);
        }

        task->packs[task->sprite_index] = RSCache_Dat2SpritePackNewDecode(
            (const unsigned char*)archive->data,
            archive->data_size,
            RSCACHE_SPRITELOAD_FLAG_NORMALIZE);
        RSCache_Dat2DiskArchiveFree(archive);
        if( !task->packs[task->sprite_index] || task->packs[task->sprite_index]->count <= 0 )
        {
            fprintf(
                stderr,
                "Failed to decode sprite pack %d for texture %d\n",
                task->def->sprite_ids[task->sprite_index],
                task->texture_id);
            task_dat2_texture_load_clear_packs(task);
            PT_EXIT(&task->pt);
        }
    }

    torirs_texture = texture_from_sprite_packs(task->packs, task->def, 128);
    task_dat2_texture_load_clear_packs(task);

    if( !torirs_texture )
    {
        fprintf(stderr, "Failed to bake dat2 texture %d\n", task->texture_id);
        PT_EXIT(&task->pt);
    }

    CacheProvider_TextureAdd(&task->bc->base, task->texture_id, torirs_texture);

    PT_END(&task->pt);
}

static void
Task_Dat2TextureLoad_Free(struct ToriRS_Task* task_base)
{
    struct Task_Dat2TextureLoad* task = (struct Task_Dat2TextureLoad*)task_base;
    task_dat2_texture_load_clear_packs(task);
    free(task);
}

static struct ToriRS_TaskVTable Task_Dat2TextureLoad_VTable = {
    .run = Task_Dat2TextureLoad_Run,
    .free = Task_Dat2TextureLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2TextureLoad(
    struct CacheProvider* provider,
    int texture_id)
{
    struct Dat2BuildCache* dat2_buildcache;
    struct Task_Dat2TextureLoad* task;

    assert(provider);

    dat2_buildcache = (struct Dat2BuildCache*)provider;
    if( CacheProvider_TextureHas(provider, texture_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2TextureLoad_VTable;
    strcpy(task->task.name, "Dat2TextureLoad");
    task->bc = dat2_buildcache;
    task->texture_id = texture_id;
    PT_INIT(&task->pt);
    return &task->task;
}
