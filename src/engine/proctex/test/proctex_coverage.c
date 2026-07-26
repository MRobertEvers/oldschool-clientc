/*
 * Procedural-texture coverage report for a 643-era cache.
 *
 * Answers the only question that matters while porting operations: *which unported
 * operation is blocking the most textures*. A texture is renderable when every
 * operation in its program is ported **and** every texture it pulls in through
 * `texture_source` is itself renderable — so the blocking op is rarely the one the
 * texture names directly, and a per-op histogram over raw programs is misleading.
 *
 * The transitive closure is computed by fixpoint: mark the textures whose own ops are
 * all ported, then repeatedly admit any texture whose dependencies are all admitted,
 * until nothing changes. Whatever is left is blocked, and each blocked texture is
 * charged to the first unported operation reachable from it.
 *
 *   make -C src test-proctex-coverage
 *   src/build/proctex_coverage [cache-dir]
 */
#include "engine/proctex/proctex_generator.h"

#include "bmp.h"
#include "datatypes/dat2_proctexture.h"
#include "datatypes/dat2_sprites.h"
#include "dat2disk.h"
#include "reference_table.h"
#include "revisions/revisions.h"
#include "rscache_profile.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TEXTURES 4096
#define BAKE_SIZE 128

struct entry
{
    struct RSCache_Dat2ProcTexture* program;
    bool present;
    /* All of this texture's own operations are ported. */
    bool ops_ok;
    /* ops_ok and every texture_source dependency is renderable. Fixpoint result. */
    bool renderable;
    /* Set when the decode stopped short of the record — a decode gap, not a port gap. */
    bool short_decode;
    /* Render results, filled by the bake pass. */
    int32_t* argb;
    bool render_attempted;
    bool render_ok;
};

static struct entry g_entries[MAX_TEXTURES];

/*
 * The first unported operation reachable from `id`, following texture_source edges.
 * Depth-first with a visit mark, so a cyclic reference terminates rather than
 * recursing forever — the format cannot express a cycle meaningfully but nothing in
 * the cache validates that.
 */
static int
first_blocking_op(int id, uint8_t* seen)
{
    if( id < 0 || id >= MAX_TEXTURES || !g_entries[id].present || seen[id] )
        return -1;
    seen[id] = 1;

    struct RSCache_Dat2ProcTexture* tex = g_entries[id].program;
    for( int i = 0; i < tex->operation_count; i++ )
        if( !ProcTexGenerator_SupportsOp(tex->operations[i].type) )
            return tex->operations[i].type;

    for( int i = 0; i < tex->texture_dependency_count; i++ )
    {
        int dep = tex->texture_dependencies[i];
        if( dep < 0 || dep >= MAX_TEXTURES || !g_entries[dep].present )
            return -2; /* dependency absent from the cache: unresolvable, not unported */
        int blocked = first_blocking_op(dep, seen);
        if( blocked != -1 )
            return blocked;
    }
    return -1;
}

/* --- bake pass ------------------------------------------------------------------------ */

/*
 * Static coverage says an evaluator exists for every operation; it says nothing about whether
 * the evaluator runs. This pass actually bakes every texture, which is what catches a port
 * that reads out of bounds, divides by zero, or never terminates.
 *
 * Dependencies are resolved the same way the client's texture task does — recursively for
 * `texture_source`, from the sprite table for `sprite_source` — with the results memoised, so
 * a texture pulled in by fifty others is evaluated once.
 */
struct bake_context
{
    struct RSCache_Dat2Disk* disk;
    int sprites_table;
    /* Flattened sprites, by id. Sparse and small; a linear scan is fine at this scale. */
    struct
    {
        int id;
        int32_t* argb;
        int width;
        int height;
    } sprites[512];
    int sprite_count;
    int depth;
};

static bool
bake_resolve_sprite(
    void* user,
    int sprite_id,
    const int32_t** out_argb,
    int* out_width,
    int* out_height);

static bool
bake_resolve_texture(void* user, int texture_id, int size, const int32_t** out_argb);

static bool
bake_texture(struct bake_context* ctx, int texture_id, int size)
{
    struct entry* entry;
    struct ProcTexGenerator* gen;
    int32_t* pixels;
    int unsupported = 0;

    if( texture_id < 0 || texture_id >= MAX_TEXTURES )
        return false;
    entry = &g_entries[texture_id];
    if( entry->render_attempted )
        return entry->render_ok;
    entry->render_attempted = true;

    if( !entry->present || !entry->renderable )
        return false;

    /* A texture_source cycle would recurse without bound; the closure is shallow in practice. */
    if( ctx->depth > 16 )
        return false;

    pixels = calloc((size_t)size * (size_t)size, sizeof(int32_t));
    if( !pixels )
        return false;

    gen = ProcTexGenerator_New(bake_resolve_sprite, bake_resolve_texture, ctx);
    if( !gen )
    {
        free(pixels);
        return false;
    }
    ctx->depth++;
    {
        bool rendered =
            ProcTexGenerator_Render(gen, entry->program, size, 1.0, pixels, &unsupported, NULL);
        entry->render_ok = rendered && unsupported == 0;
        if( !entry->render_ok && getenv("TORIRS_PROCTEX_DEBUG") )
            fprintf(
                stderr,
                "  bake %d: render=%s unsupported=%d\n",
                texture_id,
                rendered ? "true" : "false",
                unsupported);
    }
    ctx->depth--;
    ProcTexGenerator_Free(gen);

    if( !entry->render_ok )
    {
        free(pixels);
        return false;
    }
    entry->argb = pixels;
    return true;
}

static bool
bake_resolve_texture(void* user, int texture_id, int size, const int32_t** out_argb)
{
    struct bake_context* ctx = user;
    if( size != BAKE_SIZE )
        return false; /* everything here bakes at one size, so a mismatch is a port bug */
    if( !bake_texture(ctx, texture_id, size) )
        return false;
    *out_argb = g_entries[texture_id].argb;
    return true;
}

static bool
bake_resolve_sprite(
    void* user,
    int sprite_id,
    const int32_t** out_argb,
    int* out_width,
    int* out_height)
{
    struct bake_context* ctx = user;
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_Dat2SpritePack* pack;

    for( int i = 0; i < ctx->sprite_count; i++ )
        if( ctx->sprites[i].id == sprite_id )
        {
            if( !ctx->sprites[i].argb )
                return false;
            *out_argb = ctx->sprites[i].argb;
            *out_width = ctx->sprites[i].width;
            *out_height = ctx->sprites[i].height;
            return true;
        }

    if( ctx->sprite_count >= (int)(sizeof(ctx->sprites) / sizeof(ctx->sprites[0])) )
        return false;
    ctx->sprites[ctx->sprite_count].id = sprite_id;
    ctx->sprites[ctx->sprite_count].argb = NULL;
    ctx->sprite_count++;

    if( ctx->sprites_table < 0 )
        return false;
    archive = RSCache_Dat2DiskArchiveNewLoad(ctx->disk, ctx->sprites_table, sprite_id);
    if( !archive )
        return false;
    /* NORMALIZE, matching the client's texture task: the generator samples a full-size
     * sprite, so a cropped one with offsets would sample the wrong pixels. */
    pack = RSCache_Dat2SpritePackNewDecode(
        (const unsigned char*)archive->data, archive->data_size, RSCACHE_SPRITELOAD_FLAG_NORMALIZE);
    RSCache_Dat2DiskArchiveFree(archive);
    if( !pack || pack->count <= 0 )
    {
        RSCache_Dat2SpritePackFree(pack);
        return false;
    }

    {
        struct RSCache_Dat2Sprite* sprite = &pack->sprites[0];
        int count = sprite->width * sprite->height;
        int32_t* argb = count > 0 ? malloc((size_t)count * sizeof(int32_t)) : NULL;
        if( argb )
        {
            for( int i = 0; i < count; i++ )
                argb[i] = pack->palette[sprite->palette_pixels[i]];
            ctx->sprites[ctx->sprite_count - 1].argb = argb;
            ctx->sprites[ctx->sprite_count - 1].width = sprite->width;
            ctx->sprites[ctx->sprite_count - 1].height = sprite->height;
            *out_argb = argb;
            *out_width = sprite->width;
            *out_height = sprite->height;
        }
        RSCache_Dat2SpritePackFree(pack);
        return argb != NULL;
    }
}

/* --- montage ---------------------------------------------------------------------------- */

/** Does `id`'s program name `op_type`, directly or through a texture_source chain? */
static bool
uses_op(int id, int op_type, uint8_t* seen)
{
    if( id < 0 || id >= MAX_TEXTURES || !g_entries[id].present || seen[id] )
        return false;
    seen[id] = 1;
    {
        struct RSCache_Dat2ProcTexture* tex = g_entries[id].program;
        for( int i = 0; i < tex->operation_count; i++ )
            if( tex->operations[i].type == op_type )
                return true;
        for( int i = 0; i < tex->texture_dependency_count; i++ )
            if( uses_op(tex->texture_dependencies[i], op_type, seen) )
                return true;
    }
    return false;
}

#define MONTAGE_COLS 8
#define MONTAGE_ROWS 8

static void
write_montages(const char* dir)
{
    int32_t* sheet = malloc(
        (size_t)(MONTAGE_COLS * BAKE_SIZE) * (size_t)(MONTAGE_ROWS * BAKE_SIZE) * sizeof(int32_t));
    if( !sheet )
        return;

    for( int op_type = 0; op_type < RSCACHE_PROCTEX_OP_COUNT; op_type++ )
    {
        char path[1024];
        int tile = 0;

        /* Magenta background, so an empty or partly-filled sheet is unmistakable and a
         * texture that renders black is still distinguishable from "nothing drawn here". */
        for( int i = 0; i < MONTAGE_COLS * BAKE_SIZE * MONTAGE_ROWS * BAKE_SIZE; i++ )
            sheet[i] = (int32_t)0xFFFF00FF;

        for( int id = 0; id < MAX_TEXTURES && tile < MONTAGE_COLS * MONTAGE_ROWS; id++ )
        {
            uint8_t seen[MAX_TEXTURES];
            int tile_x, tile_y;
            if( !g_entries[id].argb )
                continue;
            memset(seen, 0, sizeof(seen));
            if( !uses_op(id, op_type, seen) )
                continue;

            tile_x = (tile % MONTAGE_COLS) * BAKE_SIZE;
            tile_y = (tile / MONTAGE_COLS) * BAKE_SIZE;
            for( int y = 0; y < BAKE_SIZE; y++ )
                memcpy(
                    sheet + (size_t)(tile_y + y) * (size_t)(MONTAGE_COLS * BAKE_SIZE) +
                        (size_t)tile_x,
                    g_entries[id].argb + (size_t)y * (size_t)BAKE_SIZE,
                    (size_t)BAKE_SIZE * sizeof(int32_t));
            tile++;
        }

        if( tile == 0 )
            continue;
        snprintf(path, sizeof(path), "%s/op_%02d_%s.bmp", dir, op_type, RSCache_ProcTexOpName(op_type));
        bmp_write_file(path, (int*)sheet, MONTAGE_COLS * BAKE_SIZE, MONTAGE_ROWS * BAKE_SIZE);
        printf("      montage %-20s %d textures\n", RSCache_ProcTexOpName(op_type), tile);
    }

    free(sheet);
}

int
main(int argc, char** argv)
{
    const char* dir = argc > 1 ? argv[1] : "cache.643";

    struct RSCache_Dat2Disk* disk = RSCache_Dat2DiskNewFromDirectory(dir);
    if( !disk )
    {
        printf("proctex_coverage: no cache at %s (skipping)\n", dir);
        return 0;
    }

    struct RSCache profile;
    if( !RSCache_ProfileByName("643", &profile) )
        profile = RSCache_ProfileZero();
    RSCache_Dat2DiskSetEpoch(disk, profile.epoch);

    int textures_table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_TEXTURES);
    if( textures_table < 0 )
    {
        printf("proctex_coverage: %s has no textures table\n", dir);
        RSCache_Dat2DiskFree(disk);
        return 0;
    }

    struct RSCache_Dat2DiskArchive* ref =
        RSCache_Dat2DiskArchiveNewReferenceTableLoad(disk, textures_table);
    if( !ref )
    {
        printf("proctex_coverage: %s textures reference table unreadable\n", dir);
        RSCache_Dat2DiskFree(disk);
        return 0;
    }
    struct RSCache_ReferenceTable* table =
        RSCache_ReferenceTableNewDecode(ref->data, ref->data_size);
    RSCache_Dat2DiskArchiveFree(ref);
    if( !table )
    {
        RSCache_Dat2DiskFree(disk);
        return 0;
    }

    uint32_t flags = RSCache_Dat2ProcTextureFlags(&profile);

    int total = 0;
    int decoded = 0;
    int short_decodes = 0;
    for( int i = 0; i < table->id_count; i++ )
    {
        int id = table->ids[i];
        if( id < 0 || id >= MAX_TEXTURES )
            continue;
        total++;

        struct RSCache_Dat2DiskArchive* archive =
            RSCache_Dat2DiskArchiveNewLoad(disk, textures_table, id);
        if( !archive )
            continue;

        struct RSCache_Dat2ProcTexture* program;

        /* PROCTEX_HEXDUMP=<id> prints one record's raw bytes. The decoder's own trace says
         * where it stopped; this says what it stopped on, which is what an unknown field's
         * width has to be worked out from. */
        {
            const char* want = getenv("PROCTEX_HEXDUMP");
            if( want && atoi(want) == id )
            {
                printf("texture %d: %d bytes\n", id, archive->data_size);
                for( int off = 0; off < archive->data_size; off += 16 )
                {
                    printf("  %04x:", off);
                    for( int i = 0; i < 16 && off + i < archive->data_size; i++ )
                        printf(" %02x", (unsigned char)archive->data[off + i]);
                    printf("\n");
                }
            }
        }

        program = RSCache_Dat2ProcTextureNewDecode(archive->data, archive->data_size, id, flags);
        if( program )
        {
            g_entries[id].program = program;
            g_entries[id].present = true;
            g_entries[id].short_decode = program->_consumed != archive->data_size;
            if( g_entries[id].short_decode )
                short_decodes++;
            decoded++;
        }
        RSCache_Dat2DiskArchiveFree(archive);
    }

    /* Pass 1: own operations. */
    for( int id = 0; id < MAX_TEXTURES; id++ )
    {
        if( !g_entries[id].present )
            continue;
        g_entries[id].ops_ok =
            !g_entries[id].short_decode &&
            ProcTexGenerator_IsFullySupported(g_entries[id].program, NULL);
        g_entries[id].renderable = g_entries[id].ops_ok;
    }

    /* Pass 2: fixpoint over texture_source edges. Start from "ops_ok implies renderable"
     * and retract any texture whose dependency is not renderable, until stable. */
    bool changed = true;
    while( changed )
    {
        changed = false;
        for( int id = 0; id < MAX_TEXTURES; id++ )
        {
            if( !g_entries[id].present || !g_entries[id].renderable )
                continue;
            struct RSCache_Dat2ProcTexture* tex = g_entries[id].program;
            for( int d = 0; d < tex->texture_dependency_count; d++ )
            {
                int dep = tex->texture_dependencies[d];
                bool dep_ok = dep >= 0 && dep < MAX_TEXTURES && g_entries[dep].present &&
                              g_entries[dep].renderable;
                if( !dep_ok )
                {
                    g_entries[id].renderable = false;
                    changed = true;
                    break;
                }
            }
        }
    }

    int renderable = 0;
    int blocked_by[RSCACHE_PROCTEX_OP_COUNT];
    memset(blocked_by, 0, sizeof(blocked_by));
    int blocked_missing_dep = 0;
    int blocked_short_decode = 0;
    int blocked_unknown = 0;

    /* How many textures name each unported op directly, which is a different number
     * from how many it blocks: an op used once but pulled in by fifty texture_source
     * chains blocks fifty. */
    int direct_use[RSCACHE_PROCTEX_OP_COUNT];
    memset(direct_use, 0, sizeof(direct_use));

    for( int id = 0; id < MAX_TEXTURES; id++ )
    {
        if( !g_entries[id].present )
            continue;
        struct RSCache_Dat2ProcTexture* tex = g_entries[id].program;
        for( int i = 0; i < tex->operation_count; i++ )
            if( !ProcTexGenerator_SupportsOp(tex->operations[i].type) &&
                tex->operations[i].type < RSCACHE_PROCTEX_OP_COUNT )
                direct_use[tex->operations[i].type]++;

        if( g_entries[id].renderable )
        {
            renderable++;
            continue;
        }
        if( g_entries[id].short_decode )
        {
            blocked_short_decode++;
            continue;
        }

        uint8_t seen[MAX_TEXTURES];
        memset(seen, 0, sizeof(seen));
        int op = first_blocking_op(id, seen);
        if( op == -2 )
            blocked_missing_dep++;
        else if( op >= 0 && op < RSCACHE_PROCTEX_OP_COUNT )
            blocked_by[op]++;
        else
            blocked_unknown++;
    }

    printf("proctex coverage: %s\n", dir);
    printf("  textures in table   : %d\n", total);
    printf("  programs decoded    : %d\n", decoded);
    printf("  short decodes       : %d\n", short_decodes);
    printf(
        "  renderable          : %d (%.1f%%)\n",
        renderable,
        decoded ? 100.0 * renderable / decoded : 0.0);
    printf("  blocked, short decode : %d\n", blocked_short_decode);
    printf("  blocked, missing dep  : %d\n", blocked_missing_dep);
    printf("  blocked, unattributed : %d\n", blocked_unknown);
    printf("  blocked by operation (transitive) :\n");
    for( int rank = 0; rank < RSCACHE_PROCTEX_OP_COUNT; rank++ )
    {
        int best = -1;
        for( int op = 0; op < RSCACHE_PROCTEX_OP_COUNT; op++ )
            if( blocked_by[op] > 0 && (best < 0 || blocked_by[op] > blocked_by[best]) )
                best = op;
        if( best < 0 )
            break;
        printf(
            "      %-22s (%2d) blocks %4d   named directly by %d\n",
            RSCache_ProcTexOpName(best),
            best,
            blocked_by[best],
            direct_use[best]);
        blocked_by[best] = 0;
    }

    /* Bake everything the static pass called renderable, and report the two counts apart.
     * "Renderable but did not bake" is the interesting cell: it means an evaluator exists and
     * refused, which static coverage cannot see. */
    {
        struct bake_context ctx;
        int baked = 0;
        int bake_failed = 0;
        int first_failures[16];
        int first_failure_count = 0;

        memset(&ctx, 0, sizeof(ctx));
        ctx.disk = disk;
        ctx.sprites_table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_SPRITES);

        for( int id = 0; id < MAX_TEXTURES; id++ )
        {
            if( !g_entries[id].present || !g_entries[id].renderable )
                continue;
            if( bake_texture(&ctx, id, BAKE_SIZE) )
                baked++;
            else
            {
                bake_failed++;
                if( first_failure_count < 16 )
                    first_failures[first_failure_count++] = id;
            }
        }

        printf("  baked at %dx%d      : %d\n", BAKE_SIZE, BAKE_SIZE, baked);
        printf("  renderable but failed : %d\n", bake_failed);
        /* Name the graph of each failure. "Renderable" already proved every operation has an
         * evaluator, so a failure here is a refusal from inside one — usually an unresolved
         * sprite or texture dependency — and the operation list is what identifies which. */
        for( int i = 0; i < first_failure_count; i++ )
        {
            int id = first_failures[i];
            struct RSCache_Dat2ProcTexture* tex = g_entries[id].program;
            printf(
                "      texture %4d: %d ops, colour_op=%d alpha_op=%d, deps tex=%d spr=%d\n",
                id,
                tex->operation_count,
                tex->colour_op,
                tex->alpha_op,
                tex->texture_dependency_count,
                tex->sprite_dependency_count);
            printf("        ops:");
            for( int operation = 0; operation < tex->operation_count; operation++ )
                printf(" %s", RSCache_ProcTexOpName(tex->operations[operation].type));
            printf("\n");
        }

        /*
         * PROCTEX_DUMP=<dir> writes every baked texture as a BMP. "It did not crash" is a
         * weak claim for a graphics port — a transposed index or a sign error produces a
         * perfectly valid image of the wrong thing — so the ports were checked by looking at
         * these, against what the operation is named after (bricks look like bricks).
         */
        {
            const char* dump_dir = getenv("PROCTEX_DUMP");
            if( dump_dir )
            {
                int written = 0;
                for( int id = 0; id < MAX_TEXTURES; id++ )
                {
                    char path[1024];
                    if( !g_entries[id].argb )
                        continue;
                    snprintf(path, sizeof(path), "%s/tex_%04d.bmp", dump_dir, id);
                    bmp_write_file(path, g_entries[id].argb, BAKE_SIZE, BAKE_SIZE);
                    written++;
                }
                printf("  dumped to %s      : %d bmp\n", dump_dir, written);
            }
        }

        /*
         * PROCTEX_MONTAGE=<dir> writes one sheet per operation, tiling the textures whose
         * graph uses it. This is the check that actually validates a port: bricks have to
         * look like bricks and a kaleidoscope has to be eight-fold symmetric, and a sheet of
         * sixty-four at a glance shows that far faster than opening files one at a time.
         */
        {
            const char* montage_dir = getenv("PROCTEX_MONTAGE");
            if( montage_dir )
                write_montages(montage_dir);
        }

        for( int i = 0; i < ctx.sprite_count; i++ )
            free(ctx.sprites[i].argb);
    }

    for( int id = 0; id < MAX_TEXTURES; id++ )
    {
        if( !g_entries[id].present )
            continue;
        free(g_entries[id].argb);
        RSCache_Dat2ProcTextureFree(g_entries[id].program);
    }
    RSCache_ReferenceTableFree(table);
    RSCache_Dat2DiskFree(disk);
    return 0;
}
