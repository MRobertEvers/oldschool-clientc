#include "ev_textures.h"

#include "dat2disk.h"
#include "datatypes/dat2_proctexture.h"
#include "datatypes/dat2_sprites.h"
#include "datatypes/dat2_texture.h"
#include "engine/proctex/proctex_generator.h"
#include "engine/texture_palette_bake.h"
#include "filelist.h"
#include "reference_table.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 128 is what the sprite-backed path bakes to, so both systems land at the same
 * dimensions and the kernels need one shift. */
#define EV_TEX_SIZE 128
#define EV_TEX_DEP_MAX 64

/* Above this MEAN chroma a texture is treated as carrying its own colour and is
 * not tinted. See EV_Texture.mean_chroma. */
#define EV_TEXTURE_MASK_CHROMA_MAX 40

/* ---- the flattened-sprite cache the procedural evaluator reads through ----- */

/*
 * Both resolver callbacks answer out of one table. A sprite dependency is keyed
 * by its own id; a *texture* dependency is memoised under a key that cannot
 * collide with a real sprite id, so a texture referenced by several others is
 * evaluated once rather than once per reference.
 */
struct EV_ProcSprite
{
    int key;
    int32_t* argb; /* owned; NULL memoises a failure */
    int width;
    int height;
};

struct EV_ProcCtx
{
    struct EV_ProcSprite* sprites;
    int sprite_count;
    int sprite_capacity;

    /* Decoded programs, indexed by texture id. */
    struct RSCache_Dat2ProcTexture** programs;
    int program_len;

    struct Tool_Dat2Cache* cache;
    int sprite_table;
    int texture_table;
};

static struct EV_ProcSprite*
proc_sprite_get(struct EV_ProcCtx* ctx, int key)
{
    for( int i = 0; i < ctx->sprite_count; i++ )
        if( ctx->sprites[i].key == key )
            return &ctx->sprites[i];
    return NULL;
}

static void
proc_sprite_put(struct EV_ProcCtx* ctx, int key, int32_t* argb, int width, int height)
{
    if( ctx->sprite_count == ctx->sprite_capacity )
    {
        int want = ctx->sprite_capacity ? ctx->sprite_capacity * 2 : 64;
        struct EV_ProcSprite* grown = realloc(ctx->sprites, (size_t)want * sizeof(*grown));
        assert(grown);
        ctx->sprites = grown;
        ctx->sprite_capacity = want;
    }
    ctx->sprites[ctx->sprite_count].key = key;
    ctx->sprites[ctx->sprite_count].argb = argb;
    ctx->sprites[ctx->sprite_count].width = width;
    ctx->sprites[ctx->sprite_count].height = height;
    ctx->sprite_count++;
}

static struct RSCache_Dat2ProcTexture*
proc_program_get(struct EV_ProcCtx* ctx, int id)
{
    if( id < 0 || id >= ctx->program_len )
        return NULL;
    return ctx->programs[id];
}

static bool
proc_resolve_sprite(
    void* user,
    int sprite_id,
    const int32_t** out_argb,
    int* out_width,
    int* out_height)
{
    struct EV_ProcCtx* ctx = user;
    struct EV_ProcSprite* sprite = proc_sprite_get(ctx, sprite_id);

    if( !sprite || !sprite->argb )
        return false;
    *out_argb = sprite->argb;
    *out_width = sprite->width;
    *out_height = sprite->height;
    return true;
}

static bool
proc_resolve_texture(void* user, int texture_id, int size, const int32_t** out_argb);

/*
 * A nested texture, rendered on demand.
 *
 * Brightness is 1.0, not the 0.8 the final image gets: a nested texture is an
 * intermediate signal feeding more operations, and gamma-correcting it here
 * would apply the curve twice.
 */
static bool
proc_resolve_texture(void* user, int texture_id, int size, const int32_t** out_argb)
{
    struct EV_ProcCtx* ctx = user;
    int key = 0x1000000 + (texture_id << 8) + (size & 0xFF);
    struct EV_ProcSprite* cached = proc_sprite_get(ctx, key);
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

    program = proc_program_get(ctx, texture_id);
    if( !program || !ProcTexGenerator_IsFullySupported(program, NULL) )
    {
        /* Memoise the failure too, so a broken dependency is not retried per
         * scanline of every texture that reaches it. */
        proc_sprite_put(ctx, key, NULL, 0, 0);
        return false;
    }

    pixels = calloc((size_t)size * (size_t)size, sizeof(*pixels));
    assert(pixels);

    gen = ProcTexGenerator_New(proc_resolve_sprite, proc_resolve_texture, ctx);
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
        proc_sprite_put(ctx, key, NULL, 0, 0);
        return false;
    }
    ProcTexGenerator_Free(gen);

    proc_sprite_put(ctx, key, pixels, size, size);
    *out_argb = pixels;
    return true;
}

/* ---- loading raw pieces --------------------------------------------------- */

static struct RSCache_Dat2DiskArchive*
archive_load(struct Tool_Dat2Cache* cache, int table, int archive_id)
{
    return RSCache_Dat2DiskArchiveNewLoad(cache->disk, table, archive_id);
}

/**
 * Every archive id in a table, from its reference table.
 *
 * Probing 0..N instead would both miss a sparse id space and pay a failed load
 * per hole — and an RS2 texture table is sparse. Caller frees.
 */
static int*
table_ids(struct RSCache_Dat2Disk* disk, int table_id, int* out_count)
{
    *out_count = 0;
    struct RSCache_Dat2DiskArchive* ref =
        RSCache_Dat2DiskArchiveNewReferenceTableLoad(disk, table_id);
    if( !ref )
        return NULL;
    struct RSCache_ReferenceTable* table = RSCache_ReferenceTableNewDecode(ref->data, ref->data_size);
    RSCache_Dat2DiskArchiveFree(ref);
    if( !table )
        return NULL;

    int* ids = malloc((size_t)(table->id_count > 0 ? table->id_count : 1) * sizeof(*ids));
    assert(ids);
    for( int i = 0; i < table->id_count; i++ )
        ids[i] = table->ids[i];
    *out_count = table->id_count;
    RSCache_ReferenceTableFree(table);
    return ids;
}

/**
 * Decode a sprite pack and flatten sprite 0 to ARGB, for the procedural
 * evaluator (which wants pixels, not a palette).
 */
static void
proc_load_sprite(struct EV_ProcCtx* ctx, int sprite_id)
{
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_Dat2SpritePack* pack;

    if( proc_sprite_get(ctx, sprite_id) )
        return;

    archive = archive_load(ctx->cache, ctx->sprite_table, sprite_id);
    if( !archive )
    {
        proc_sprite_put(ctx, sprite_id, NULL, 0, 0);
        return;
    }

    pack = RSCache_Dat2SpritePackNewDecode(
        (const unsigned char*)archive->data, archive->data_size, RSCACHE_SPRITELOAD_FLAG_NORMALIZE);
    RSCache_Dat2DiskArchiveFree(archive);

    if( pack && pack->count > 0 )
    {
        struct RSCache_Dat2Sprite* sprite = &pack->sprites[0];
        int count = sprite->width * sprite->height;
        int32_t* argb = malloc((size_t)(count > 0 ? count : 1) * sizeof(*argb));
        assert(argb);
        for( int i = 0; i < count; i++ )
            argb[i] = pack->palette[sprite->palette_pixels[i]];
        proc_sprite_put(ctx, sprite_id, argb, sprite->width, sprite->height);
    }
    else
    {
        proc_sprite_put(ctx, sprite_id, NULL, 0, 0);
    }
    if( pack )
        RSCache_Dat2SpritePackFree(pack);
}

/* Append to a worklist, skipping ids already queued. Bounded so a pathological
 * dependency graph cannot grow the walk without limit. */
static bool
dep_queue(int* list, int* count, int capacity, int value)
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

static struct RSCache_Dat2ProcTexture*
proc_program_load(struct EV_ProcCtx* ctx, int id, int flags)
{
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_Dat2ProcTexture* program;

    if( id < 0 || id >= ctx->program_len )
        return NULL;
    if( ctx->programs[id] )
        return ctx->programs[id];

    archive = archive_load(ctx->cache, ctx->texture_table, id);
    if( !archive )
        return NULL;

    program = RSCache_Dat2ProcTextureNewDecode(archive->data, archive->data_size, id, flags);
    RSCache_Dat2DiskArchiveFree(archive);
    ctx->programs[id] = program;
    return program;
}

/**
 * Make everything one texture's graph can reach resident, then evaluate it.
 *
 * The dependency walk has to complete before rendering starts: the generator
 * runs to completion in one call, so a texture or sprite fetched lazily from
 * inside it is not expressible. The worklist grows while being walked (a
 * dependency has dependencies) and residency is what terminates it, so a cyclic
 * texture_source reference is naturally bounded.
 */
static int32_t*
proc_bake(struct EV_ProcCtx* ctx, int texture_id, int flags, bool* out_opaque)
{
    int dep_textures[EV_TEX_DEP_MAX];
    int dep_texture_count = 0;
    int dep_sprites[EV_TEX_DEP_MAX];
    int dep_sprite_count = 0;
    struct RSCache_Dat2ProcTexture* def;
    struct ProcTexGenerator* gen;
    int32_t* pixels;
    int unsupported = 0;
    bool transparent = false;

    if( !proc_program_load(ctx, texture_id, flags) )
        return NULL;

    dep_queue(dep_textures, &dep_texture_count, EV_TEX_DEP_MAX, texture_id);
    for( int cursor = 0; cursor < dep_texture_count; cursor++ )
    {
        struct RSCache_Dat2ProcTexture* dep = proc_program_load(ctx, dep_textures[cursor], flags);
        if( !dep )
            continue;
        for( int i = 0; i < dep->texture_dependency_count; i++ )
            dep_queue(
                dep_textures, &dep_texture_count, EV_TEX_DEP_MAX, dep->texture_dependencies[i]);
        for( int i = 0; i < dep->sprite_dependency_count; i++ )
            dep_queue(dep_sprites, &dep_sprite_count, EV_TEX_DEP_MAX, dep->sprite_dependencies[i]);
    }

    for( int i = 0; i < dep_sprite_count; i++ )
        proc_load_sprite(ctx, dep_sprites[i]);

    def = proc_program_get(ctx, texture_id);
    if( !def )
        return NULL;

    /*
     * Refuse rather than approximate. An operation with no evaluator contributes
     * flat mid-grey, which composites into something that looks like a real
     * texture and is not; a missing texture makes the face fall back to flat
     * shading, which reads as "not textured" rather than confidently wrong.
     */
    if( !ProcTexGenerator_IsFullySupported(def, NULL) )
        return NULL;

    pixels = calloc((size_t)EV_TEX_SIZE * (size_t)EV_TEX_SIZE, sizeof(*pixels));
    assert(pixels);

    gen = ProcTexGenerator_New(proc_resolve_sprite, proc_resolve_texture, ctx);
    if( !gen )
    {
        free(pixels);
        return NULL;
    }
    /* 0.8 matches the gamma the sprite-backed path applies, so the two systems
     * land at a comparable brightness in the same scene. */
    if( !ProcTexGenerator_Render(gen, def, EV_TEX_SIZE, 0.8, pixels, &unsupported, &transparent) ||
        unsupported > 0 )
    {
        ProcTexGenerator_Free(gen);
        free(pixels);
        return NULL;
    }
    ProcTexGenerator_Free(gen);

    if( out_opaque )
        *out_opaque = !transparent;
    return pixels;
}

/* ---- the sprite-backed system --------------------------------------------- */

/**
 * Layer one texture definition's sprites into 128x128 ARGB.
 *
 * The palette adjustment (gamma 0.8, and the `& 0xf8f8ff` test that makes
 * near-black transparent) is the reference's, and the 64->128 rescale is the
 * reference's addressing — column-major into a row-major destination, which
 * looks like a bug and is what the game does.
 */
static int32_t*
classic_bake(
    const struct RSCache_Dat2Texture* def,
    struct RSCache_Dat2SpritePack* const* packs,
    bool* out_opaque)
{
    int32_t* pixels = calloc((size_t)EV_TEX_SIZE * (size_t)EV_TEX_SIZE, sizeof(*pixels));
    bool opaque = true;

    assert(pixels);

    for( int layer = 0; layer < def->sprite_ids_count; layer++ )
    {
        struct RSCache_Dat2SpritePack* pack = packs[layer];
        struct RSCache_Dat2Sprite* sprite;
        int* adjusted;
        int blend_type = 0;

        if( !pack || pack->count <= 0 )
            continue;
        sprite = &pack->sprites[0];
        if( !sprite->palette_pixels || pack->palette_length <= 0 )
            continue;

        if( layer > 0 && def->sprite_types )
            blend_type = def->sprite_types[layer - 1];
        /* Only blend type 0 (replace) is implemented, matching the client. */
        if( blend_type != 0 )
            continue;

        adjusted = malloc((size_t)pack->palette_length * sizeof(*adjusted));
        assert(adjusted);
        for( int pi = 0; pi < pack->palette_length; pi++ )
        {
            int alpha = ((pack->palette[pi] & 0xf8f8ff) == 0) ? 0 : 0xff;
            adjusted[pi] = (alpha << 24) | ToriRS_TextureGammaBlend(pack->palette[pi], 0.8);
        }

        for( int i = 0; i < sprite->width * sprite->height; i++ )
        {
            int pi = sprite->palette_pixels[i];
            if( pi >= 0 && pi < pack->palette_length && (adjusted[pi] & 0xf8f8ff) == 0 )
                opaque = false;
        }

        if( sprite->width == EV_TEX_SIZE && sprite->height == EV_TEX_SIZE )
        {
            for( int i = 0; i < EV_TEX_SIZE * EV_TEX_SIZE; i++ )
            {
                int pi = sprite->palette_pixels[i];
                if( pi >= 0 && pi < pack->palette_length )
                    pixels[i] = adjusted[pi];
            }
        }
        else if( sprite->width == 64 && sprite->height == 64 )
        {
            int out = 0;
            for( int x = 0; x < EV_TEX_SIZE; x++ )
            {
                for( int y = 0; y < EV_TEX_SIZE; y++ )
                {
                    int src = ((x >> 1) << 6) + (y >> 1);
                    int pi = sprite->palette_pixels[src];
                    if( pi >= 0 && pi < pack->palette_length )
                        pixels[out] = adjusted[pi];
                    out++;
                }
            }
        }
        free(adjusted);
    }

    if( out_opaque )
        *out_opaque = opaque;
    return pixels;
}

/* ---- the set --------------------------------------------------------------- */

static void
set_put(
    struct EV_TextureSet* set,
    int id,
    int32_t* texels,
    bool opaque,
    bool procedural,
    const struct RSCache_Dat2Material* mat)
{
    if( set->count == set->capacity )
    {
        int want = set->capacity ? set->capacity * 2 : 256;
        struct EV_Texture* grown = realloc(set->items, (size_t)want * sizeof(*grown));
        assert(grown);
        set->items = grown;
        set->capacity = want;
    }
    set->items[set->count].id = id;
    set->items[set->count].texels = texels;
    set->items[set->count].size = EV_TEX_SIZE;
    set->items[set->count].opaque = opaque;
    set->items[set->count].procedural = procedural;

    /* Weighted the way the eye weights it, so a green-dominant detail map is
     * not treated as darker than it looks. Clamped away from 0: a neutral of
     * zero would make the tint a divide-by-zero and, worse, silently fall back
     * to the global default for the one texture most in need of its own. */
    {
        long long sum = 0;
        int n = EV_TEX_SIZE * EV_TEX_SIZE;
        long long chroma_sum = 0;
        for( int i = 0; i < n; i++ )
        {
            int px = texels[i];
            int r = (px >> 16) & 0xFF, g = (px >> 8) & 0xFF, b = px & 0xFF;
            sum += (2126 * r + 7152 * g + 722 * b) / 10000;

            int hi = r > g ? (r > b ? r : b) : (g > b ? g : b);
            int lo = r < g ? (r < b ? r : b) : (g < b ? g : b);
            chroma_sum += hi - lo;
        }
        int mean = (int)(sum / n);
        set->items[set->count].mean_luma = mean < 1 ? 1 : (mean > 255 ? 255 : mean);
        set->items[set->count].mean_chroma = (int)(chroma_sum / n);
    }

    if( mat && mat->exists )
    {
        set->items[set->count].alpha_mode = mat->alpha_mode;
        set->items[set->count].repeat_s = mat->repeat_s;
        set->items[set->count].repeat_t = mat->repeat_t;
        /*
         * Tint only a texture that has no hue of its own.
         *
         * A mask (chroma ~0) is meaningless untinted; a picture is already
         * coloured, and multiplying its hue by the face's applies the colour
         * twice and clamps. The threshold is deliberately generous — a detail
         * map can carry a slight cast without being a picture.
         */
        set->items[set->count].modulate =
            set->items[set->count].mean_chroma < EV_TEXTURE_MASK_CHROMA_MAX;
    }
    else
    {
        /* No material to ask: fall back to the texels, and do not tint — the
         * sprite-backed reference takes the texel as the colour outright. */
        set->items[set->count].alpha_mode = opaque ? 0 : 2;
        set->items[set->count].repeat_s = true;
        set->items[set->count].repeat_t = true;
        set->items[set->count].modulate = false;
    }
    set->count++;
}

/** Build the id -> slot map once the ids are all known. */
static void
set_index(struct EV_TextureSet* set)
{
    int max_id = -1;
    for( int i = 0; i < set->count; i++ )
        if( set->items[i].id > max_id )
            max_id = set->items[i].id;
    if( max_id < 0 )
        return;

    set->by_id_len = max_id + 1;
    set->by_id = malloc((size_t)set->by_id_len * sizeof(*set->by_id));
    assert(set->by_id);
    for( int i = 0; i < set->by_id_len; i++ )
        set->by_id[i] = -1;
    for( int i = 0; i < set->count; i++ )
        set->by_id[set->items[i].id] = i;
}

static bool
load_procedural(
    struct Tool_Dat2Cache* cache,
    struct EV_TextureSet* out,
    int texture_table,
    const struct RSCache_Dat2MaterialTable* materials)
{
    struct EV_ProcCtx ctx;
    int flags = RSCache_Dat2ProcTextureFlags(&cache->profile);
    int* ids = NULL;
    int id_count = 0;

    memset(&ctx, 0, sizeof(ctx));
    ctx.cache = cache;
    ctx.texture_table = texture_table;
    ctx.sprite_table = RSCache_Dat2DiskTableId(cache->disk, RSCACHE_DAT2_TABLE_SPRITES);

    ids = table_ids(cache->disk, texture_table, &id_count);
    if( !ids )
        return false;

    /* Programs are indexed by id, so the array spans the id space rather than
     * the count — an RS2 texture table is dense enough for that to be cheap. */
    ctx.program_len = 0;
    for( int i = 0; i < id_count; i++ )
        if( ids[i] + 1 > ctx.program_len )
            ctx.program_len = ids[i] + 1;
    ctx.programs = calloc((size_t)(ctx.program_len > 0 ? ctx.program_len : 1), sizeof(*ctx.programs));
    assert(ctx.programs);

    for( int i = 0; i < id_count; i++ )
    {
        bool opaque = true;
        int32_t* texels = proc_bake(&ctx, ids[i], flags, &opaque);
        if( texels )
        {
            const struct RSCache_Dat2Material* mat =
                (materials && ids[i] >= 0 && ids[i] < materials->count)
                    ? &materials->materials[ids[i]]
                    : NULL;
            set_put(out, ids[i], texels, opaque, true, mat);
            out->loaded++;
        }
        else
        {
            out->failed++;
        }
    }

    for( int i = 0; i < ctx.program_len; i++ )
        if( ctx.programs[i] )
            RSCache_Dat2ProcTextureFree(ctx.programs[i]);
    free(ctx.programs);
    for( int i = 0; i < ctx.sprite_count; i++ )
        free(ctx.sprites[i].argb);
    free(ctx.sprites);
    free(ids);
    return true;
}

static bool
load_sprite_backed(struct Tool_Dat2Cache* cache, struct EV_TextureSet* out, int texture_table)
{
    int sprite_table = RSCache_Dat2DiskTableId(cache->disk, RSCACHE_DAT2_TABLE_SPRITES);
    struct RSCache_Dat2DiskArchive* archive = archive_load(cache, texture_table, 0);
    struct RSCache_FileList* filelist;

    if( !archive )
        return false;

    /*
     * Every definition is a *file inside* one archive, so the archive's child
     * list is what turns bytes into per-id records. The plain loader returns
     * only the payload — without this the file count is 0 and the whole table
     * silently yields nothing.
     */
    if( !RSCache_Dat2DiskArchiveInitMetadata(cache->disk, archive) || archive->file_count <= 0 )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return false;
    }

    filelist =
        RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !filelist )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return false;
    }

    for( int i = 0; i < filelist->file_count; i++ )
    {
        int id = archive->file_ids[i];
        struct RSCache_Dat2Texture* def;
        struct RSCache_Dat2SpritePack** packs;
        int32_t* texels;
        bool opaque = true;
        bool ok = true;

        def = RSCache_Dat2TextureNewDecodeProfile(
            &cache->profile, filelist->files[i], filelist->file_sizes[i]);
        if( !def )
        {
            out->failed++;
            continue;
        }
        if( def->sprite_ids_count <= 0 )
        {
            RSCache_Dat2TextureFree(def);
            out->failed++;
            continue;
        }

        packs = calloc((size_t)def->sprite_ids_count, sizeof(*packs));
        assert(packs);

        for( int s = 0; s < def->sprite_ids_count; s++ )
        {
            struct RSCache_Dat2DiskArchive* sprite_archive =
                archive_load(cache, sprite_table, def->sprite_ids[s]);
            if( !sprite_archive )
            {
                ok = false;
                break;
            }
            packs[s] = RSCache_Dat2SpritePackNewDecode(
                (const unsigned char*)sprite_archive->data,
                sprite_archive->data_size,
                RSCACHE_SPRITELOAD_FLAG_NORMALIZE);
            RSCache_Dat2DiskArchiveFree(sprite_archive);
            if( !packs[s] || packs[s]->count <= 0 )
            {
                ok = false;
                break;
            }
        }

        texels = ok ? classic_bake(def, packs, &opaque) : NULL;
        if( texels )
        {
            set_put(out, id, texels, opaque, false, NULL);
            out->loaded++;
        }
        else
        {
            out->failed++;
        }

        for( int s = 0; s < def->sprite_ids_count; s++ )
            if( packs[s] )
                RSCache_Dat2SpritePackFree(packs[s]);
        free(packs);
        RSCache_Dat2TextureFree(def);
    }

    RSCache_FileListFree(filelist);
    RSCache_Dat2DiskArchiveFree(archive);
    return true;
}

bool
ev_textures_load(struct Tool_Dat2Cache* cache, struct EV_TextureSet* out)
{
    int texture_table;
    bool ok;

    assert(cache && out);
    memset(out, 0, sizeof(*out));

    texture_table = RSCache_Dat2DiskTableId(cache->disk, RSCACHE_DAT2_TABLE_TEXTURES);
    if( texture_table < 0 )
        return false;

    /*
     * Probe rather than infer from the revision. The materials table existing is
     * the gate the client uses, and a cache can be new enough for procedural
     * textures and still not ship one.
     */
    /*
     * The materials table is both the gate for "is this cache procedural" and
     * the source of every compositing decision, so it is decoded and KEPT
     * rather than probed and thrown away. Without it the gate has to be guessed
     * from the baked texels, which is wrong for a procedural texture: partial
     * alpha is an ordinary intermediate of its own graph, not a statement that
     * the surface is see-through.
     */
    struct RSCache_Dat2MaterialTable* materials = NULL;
    out->procedural_system = RSCache_Dat2UsesProcTextures(&cache->profile, true);
    if( out->procedural_system )
    {
        int materials_table = RSCache_Dat2DiskTableId(cache->disk, RSCACHE_DAT2_TABLE_MATERIALS);
        struct RSCache_Dat2DiskArchive* archive =
            materials_table >= 0 ? archive_load(cache, materials_table, 0) : NULL;
        if( archive )
        {
            materials = RSCache_Dat2MaterialTableNewDecode(
                archive->data, archive->data_size,
                RSCache_Dat2ProcTextureFlags(&cache->profile));
            RSCache_Dat2DiskArchiveFree(archive);
        }
        /* The reference gates on the table decoding, not merely existing. */
        out->procedural_system = materials != NULL;
    }

    ok = out->procedural_system ? load_procedural(cache, out, texture_table, materials)
                                : load_sprite_backed(cache, out, texture_table);
    out->material_count = materials ? materials->count : 0;
    if( materials )
        RSCache_Dat2MaterialTableFree(materials);
    set_index(out);

    int blend = 0, cutout = 0, clamped = 0, tinted = 0;
    for( int i = 0; i < out->count; i++ )
    {
        if( out->items[i].alpha_mode == 2 ) blend++;
        else if( out->items[i].alpha_mode == 1 ) cutout++;
        if( !out->items[i].repeat_s || !out->items[i].repeat_t ) clamped++;
        if( out->items[i].modulate ) tinted++;
    }
    fprintf(
        stderr,
        "textures: %s system — %d loaded, %d failed, %d blend, %d cutout, "
        "%d clamped, %d tinted (masks)\n",
        out->procedural_system ? "procedural (RS2 materials)" : "sprite-backed",
        out->loaded, out->failed, blend, cutout, clamped, tinted);
    return ok;
}

const struct EV_Texture*
ev_textures_get(const struct EV_TextureSet* set, int id)
{
    int slot;

    assert(set);
    if( !set->by_id || id < 0 || id >= set->by_id_len )
        return NULL;
    slot = set->by_id[id];
    if( slot < 0 )
        return NULL;
    if( !set->items[slot].texels )
        return NULL;
    return &set->items[slot];
}

void
ev_textures_free(struct EV_TextureSet* set)
{
    if( !set )
        return;
    for( int i = 0; i < set->count; i++ )
        free(set->items[i].texels);
    free(set->items);
    free(set->by_id);
    memset(set, 0, sizeof(*set));
}
