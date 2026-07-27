/*
 * Materials: dat2 texture records -> LostCity texture PNGs.
 *
 * A dat2 texture is a tiny record naming a sprite, and that sprite is already a
 * palette plus a byte-per-pixel index buffer — which is exactly the shape rev
 * 254 stores a texture in (Pix8: one "<id>.dat" image per texture in the
 * TEXTURES jagfile, index 0 transparent). So this is a transliteration rather
 * than a rasterisation, and the pixels survive it unchanged.
 *
 * What rev 254 imposes, checked against Client-TS Pix3D/Pix8 and LostCity's own
 * PixPack:
 *
 *   - **The canvas is 128x128 or 64x64.** Pix3D.getTexels copies 16384 texels
 *     flat, or upsamples 2x when the width is 64. Nothing else is expressible.
 *     Every osrs239 material is already one of the two.
 *   - **At most 128 palette entries, index 0 transparent.** Pix8 reads its pixel
 *     indices out of an Int8Array, so an index of 128 or more comes back
 *     negative and the palette lookup yields NaN — a silently black texture.
 *     LostCity's packer would happily emit up to 255, so the cap has to be
 *     enforced here, on the way out.
 *   - **Transparency is by colour, not by channel.** LostCity's PNG packer takes
 *     0xFF00FF as the transparent entry and gives it index 0, matching the
 *     source's own "index 0 is transparent" rule. A source colour that happens
 *     to *be* magenta would be punched out, so it is nudged.
 *   - **Black is transparent too.** The renderer marks any texel that is zero
 *     after `& 0xf8f8ff` as translucent and the raster skips it, so a material
 *     that the source calls opaque must not carry one.
 *
 * Ids come from `texture.pack` like every other asset. The 2004 client stopped
 * at 50 because that is what its cache shipped; the ceiling that actually
 * matters is the flo config's one-byte `texture` opcode.
 */

#include "lc_export.h"

#include <miniz.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Palette entries a rev-254 texture can address, transparent entry included. */
#define LC_TEXTURE_MAX_PALETTE 128

/** LostCity's PixPack transparency sentinel, which becomes palette index 0. */
#define LC_TEXTURE_TRANSPARENT_RGB 0xFF00FF

/** Highest id a `.flo` can name: FloConfig writes the texture opcode as a byte. */
#define LC_MAX_TEXTURE_ID 255

/* ---- source table ------------------------------------------------------- */

void
lc_textures_load(struct LC_Ctx* ctx)
{
    assert(ctx);

    int table = RSCache_Dat2DiskTableId(ctx->src->disk, RSCACHE_DAT2_TABLE_TEXTURES);
    if( table == RSCACHE_DAT2_DISK_TABLE_ABSENT )
        return;
    struct RSCache_Dat2DiskArchive* archive =
        RSCache_Dat2DiskArchiveNewLoad(ctx->src->disk, table, 0);
    if( !archive )
        return;
    if( !RSCache_Dat2DiskArchiveInitMetadata(ctx->src->disk, archive) ||
        archive->file_count <= 0 )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return;
    }

    /* The texture group's own revision selects the record codec — the modern
     * single-sprite layout against the older multi-sprite one. */
    struct RSCache local = ctx->src->profile;
    RSCache_ProfileSetGroupRevision(&local, RSCACHE_TYPE_TEXTURE, archive->revision);

    struct RSCache_FileList* files =
        RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !files )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return;
    }

    int max_id = 0;
    for( int i = 0; i < archive->file_count; i++ )
    {
        int file_id = archive->file_ids ? archive->file_ids[i] : i;
        if( file_id > max_id )
            max_id = file_id;
    }
    ctx->textures = calloc((size_t)max_id + 1, sizeof(*ctx->textures));
    if( !ctx->textures )
    {
        RSCache_FileListFree(files);
        RSCache_Dat2DiskArchiveFree(archive);
        return;
    }
    ctx->texture_count = max_id + 1;
    for( int i = 0; i < ctx->texture_count; i++ )
    {
        ctx->textures[i].sprite_id = -1;
        ctx->textures[i].average_hsl = -1;
    }

    for( int i = 0; i < files->file_count; i++ )
    {
        if( files->file_sizes[i] <= 0 )
            continue;
        int file_id = (archive->file_ids && i < archive->file_count) ? archive->file_ids[i] : i;
        if( file_id < 0 || file_id >= ctx->texture_count )
            continue;
        struct RSCache_Dat2Texture* tex = RSCache_Dat2TextureNewDecodeProfile(
            &local, files->files[i], files->file_sizes[i]);
        if( !tex )
            continue;
        ctx->textures[file_id].average_hsl = tex->average_hsl;
        ctx->textures[file_id].opaque = tex->opaque ? 1 : 0;
        if( tex->sprite_ids_count > 0 )
            ctx->textures[file_id].sprite_id = tex->sprite_ids[0];
        RSCache_Dat2TextureFree(tex);
    }

    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
}

int
lc_texture_average_hsl(
    const struct LC_Ctx* ctx,
    int src_texture_id)
{
    assert(ctx);
    if( src_texture_id < 0 || src_texture_id >= ctx->texture_count )
        return ctx->texture_fallback_hsl;
    if( ctx->textures[src_texture_id].average_hsl < 0 )
        return ctx->texture_fallback_hsl;
    return ctx->textures[src_texture_id].average_hsl;
}

/* ---- colour reduction --------------------------------------------------- */

/*
 * Fit a texture's colours into the palette rev 254 can address.
 *
 * Agglomerative rather than median cut, because the reduction is tiny: the
 * widest source palette is 256 and the target is 127, so this is a handful of
 * merges over an already-small set, and repeatedly collapsing the two nearest
 * colours keeps far more of the original than carving the cube into boxes
 * would. Counts weight the merge, so a colour covering half the texture pulls
 * the merged value towards itself instead of being averaged away by a colour
 * used on four pixels.
 */
struct lc_colour_bin
{
    /* Sums rather than a mean, so merging is exact and order-independent. */
    long r_sum;
    long g_sum;
    long b_sum;
    long count;
    int alive;
};

static int
lc_colour_bin_rgb(const struct lc_colour_bin* bin)
{
    int r = (int)((bin->r_sum + bin->count / 2) / bin->count);
    int g = (int)((bin->g_sum + bin->count / 2) / bin->count);
    int b = (int)((bin->b_sum + bin->count / 2) / bin->count);
    return (r << 16) | (g << 8) | b;
}

static long
lc_colour_bin_distance(
    const struct lc_colour_bin* a,
    const struct lc_colour_bin* b)
{
    int rgb_a = lc_colour_bin_rgb(a);
    int rgb_b = lc_colour_bin_rgb(b);
    long dr = ((rgb_a >> 16) & 0xFF) - ((rgb_b >> 16) & 0xFF);
    long dg = ((rgb_a >> 8) & 0xFF) - ((rgb_b >> 8) & 0xFF);
    long db = (rgb_a & 0xFF) - (rgb_b & 0xFF);
    /* Rec. 601 luma weights: green errors read as much larger than blue ones. */
    return 3 * dr * dr + 6 * dg * dg + 1 * db * db;
}

/**
 * Reduce `colours` (with `counts` pixels each) to at most `limit` distinct
 * values, rewriting each entry to the value it merged into. Returns how many
 * distinct values remain.
 */
static int
lc_reduce_colours(
    int* colours,
    const long* counts,
    int count,
    int limit)
{
    struct lc_colour_bin* bins = calloc((size_t)count, sizeof(*bins));
    int* owner = malloc((size_t)count * sizeof(int));
    int alive = count;

    if( !bins || !owner )
    {
        free(bins);
        free(owner);
        return count;
    }

    for( int i = 0; i < count; i++ )
    {
        long n = counts[i] > 0 ? counts[i] : 1;
        bins[i].r_sum = (long)((colours[i] >> 16) & 0xFF) * n;
        bins[i].g_sum = (long)((colours[i] >> 8) & 0xFF) * n;
        bins[i].b_sum = (long)(colours[i] & 0xFF) * n;
        bins[i].count = n;
        bins[i].alive = 1;
        owner[i] = i;
    }

    while( alive > limit )
    {
        long best = -1;
        int best_i = -1;
        int best_j = -1;

        for( int i = 0; i < count; i++ )
        {
            if( !bins[i].alive )
                continue;
            for( int j = i + 1; j < count; j++ )
            {
                if( !bins[j].alive )
                    continue;
                long d = lc_colour_bin_distance(&bins[i], &bins[j]);
                if( best < 0 || d < best )
                {
                    best = d;
                    best_i = i;
                    best_j = j;
                }
            }
        }
        if( best_i < 0 )
            break;

        bins[best_i].r_sum += bins[best_j].r_sum;
        bins[best_i].g_sum += bins[best_j].g_sum;
        bins[best_i].b_sum += bins[best_j].b_sum;
        bins[best_i].count += bins[best_j].count;
        bins[best_j].alive = 0;
        for( int k = 0; k < count; k++ )
        {
            if( owner[k] == best_j )
                owner[k] = best_i;
        }
        alive--;
    }

    for( int i = 0; i < count; i++ )
        colours[i] = lc_colour_bin_rgb(&bins[owner[i]]);

    free(bins);
    free(owner);
    return alive;
}

/* ---- export ------------------------------------------------------------- */

/*
 * Move a colour off the two values rev 254 reserves.
 *
 * Magenta is LostCity's transparency sentinel, so a texel carrying it would be
 * punched out. Zero after the renderer's `& 0xf8f8ff` mask marks the whole
 * texture translucent and the raster skips the texel, so near-black is a hole
 * too. Both are one-step nudges: the smallest change that clears the reserved
 * value, well under the quantisation the palette already applies.
 */
static int
lc_texture_safe_rgb(int rgb)
{
    rgb &= 0xFFFFFF;
    if( rgb == LC_TEXTURE_TRANSPARENT_RGB )
        return 0xFF08FF;
    if( (rgb & 0xF8F8FF) == 0 )
        return rgb | 0x080800;
    return rgb;
}

static struct RSCache_Dat2SpritePack*
lc_texture_sprite(
    struct LC_Ctx* ctx,
    int sprite_id)
{
    struct Tool_Bytes bytes;
    int table = RSCache_Dat2DiskTableId(ctx->src->disk, RSCACHE_DAT2_TABLE_SPRITES);
    if( table == RSCACHE_DAT2_DISK_TABLE_ABSENT )
        return NULL;
    if( !tool_dat2_archive_bytes(ctx->src, table, sprite_id, &bytes) )
        return NULL;
    /* Normalised: the pixel buffer comes back as the full canvas with index 0
     * in the border, which is the layout a rev-254 texture wants anyway. */
    struct RSCache_Dat2SpritePack* pack = RSCache_Dat2SpritePackNewDecode(
        bytes.data, bytes.size, RSCACHE_SPRITELOAD_FLAG_NORMALIZE);
    tool_bytes_free(&bytes);
    return pack;
}

/*
 * Remember that a material will not be ported.
 *
 * Recorded in the same map a success goes into, because the caller asks once
 * per *face*: without this a refused texture re-reads its sprite archive and
 * re-emits its warning a few thousand times over a map square.
 */
static int
lc_texture_refuse(
    struct LC_Ctx* ctx,
    int src_texture_id)
{
    lc_id_map_put(&ctx->texture_map, src_texture_id, -1);
    return -1;
}

int
lc_export_texture(
    struct LC_Ctx* ctx,
    int src_texture_id,
    const char* requested_name)
{
    assert(ctx);

    int existing;
    if( lc_id_map_get(&ctx->texture_map, src_texture_id, &existing) )
        return existing;

    char name[96];
    if( requested_name && requested_name[0] )
        snprintf(name, sizeof(name), "%s", requested_name);
    else
        snprintf(name, sizeof(name), "%s_tex_%d", ctx->out->prefix, src_texture_id);

    if( src_texture_id < 0 || src_texture_id >= ctx->texture_count ||
        ctx->textures[src_texture_id].sprite_id < 0 )
    {
        lc_out_warn(ctx->out, "texture %d: not in source cache", src_texture_id);
        return lc_texture_refuse(ctx, src_texture_id);
    }

    /*
     * Checked before the decode, not after: refusing late would leave the PNG on
     * disk with no pack line naming it, which the LostCity packer treats as a
     * hard error ("<name> is missing an ID line").
     */
    if( lc_pack_find(&ctx->packs->packs[LC_PACK_TEXTURE], name) < 0 &&
        ctx->packs->packs[LC_PACK_TEXTURE].max >= ctx->max_textures )
    {
        lc_out_warn(
            ctx->out,
            "texture %d: next free texture id is %d and the limit is %d;"
            " faces using it fall back to its average colour (--max-textures raises it)",
            src_texture_id,
            ctx->packs->packs[LC_PACK_TEXTURE].max,
            ctx->max_textures);
        return lc_texture_refuse(ctx, src_texture_id);
    }

    struct RSCache_Dat2SpritePack* pack =
        lc_texture_sprite(ctx, ctx->textures[src_texture_id].sprite_id);
    if( !pack || pack->count <= 0 )
    {
        if( pack )
            RSCache_Dat2SpritePackFree(pack);
        lc_out_warn(
            ctx->out,
            "texture %d: sprite %d missing or undecodable",
            src_texture_id,
            ctx->textures[src_texture_id].sprite_id);
        return lc_texture_refuse(ctx, src_texture_id);
    }

    struct RSCache_Dat2Sprite* sprite = &pack->sprites[0];
    int size = sprite->crop_width;
    if( sprite->crop_width != sprite->crop_height || (size != 64 && size != 128) )
    {
        lc_out_warn(
            ctx->out,
            "texture %d: %dx%d is neither 128x128 nor 64x64; rev 254 cannot hold it",
            src_texture_id,
            sprite->crop_width,
            sprite->crop_height);
        RSCache_Dat2SpritePackFree(pack);
        return lc_texture_refuse(ctx, src_texture_id);
    }

    int pixel_count = size * size;

    /* Distinct opaque colours, and how much of the texture each covers. The
     * palette is at most 256 entries, so a direct histogram over it is enough —
     * no need to walk the pixels for anything but the counts. */
    int* used = calloc((size_t)pack->palette_length, sizeof(int));
    long* counts = calloc((size_t)pack->palette_length, sizeof(long));
    if( !used || !counts )
    {
        free(used);
        free(counts);
        RSCache_Dat2SpritePackFree(pack);
        return -1;
    }
    for( int i = 0; i < pixel_count; i++ )
    {
        int index = sprite->palette_pixels[i];
        if( index <= 0 || index >= pack->palette_length )
            continue;
        used[index] = 1;
        counts[index]++;
    }

    int distinct = 0;
    for( int i = 1; i < pack->palette_length; i++ )
        distinct += used[i] ? 1 : 0;

    /* Palette index 0 is the transparent entry in both formats, so the colours
     * have to fit in what is left. */
    int limit = LC_TEXTURE_MAX_PALETTE - 1;
    int* remap = calloc((size_t)pack->palette_length, sizeof(int));
    if( !remap )
    {
        free(used);
        free(counts);
        RSCache_Dat2SpritePackFree(pack);
        return -1;
    }
    for( int i = 0; i < pack->palette_length; i++ )
        remap[i] = lc_texture_safe_rgb(pack->palette[i]);

    if( distinct > limit )
    {
        int* dense = malloc((size_t)distinct * sizeof(int));
        long* dense_counts = malloc((size_t)distinct * sizeof(long));
        int* back = malloc((size_t)distinct * sizeof(int));
        if( dense && dense_counts && back )
        {
            int n = 0;
            for( int i = 1; i < pack->palette_length; i++ )
            {
                if( !used[i] )
                    continue;
                dense[n] = remap[i];
                dense_counts[n] = counts[i];
                back[n] = i;
                n++;
            }
            int kept = lc_reduce_colours(dense, dense_counts, n, limit);
            for( int i = 0; i < n; i++ )
                remap[back[i]] = lc_texture_safe_rgb(dense[i]);
            lc_out_warn(
                ctx->out,
                "texture %d: %d colours reduced to %d; rev 254 addresses %d per texture",
                src_texture_id,
                distinct,
                kept,
                limit);
        }
        free(dense);
        free(dense_counts);
        free(back);
    }

    uint8_t* rgb = malloc((size_t)pixel_count * 3);
    if( !rgb )
    {
        free(used);
        free(counts);
        free(remap);
        RSCache_Dat2SpritePackFree(pack);
        return -1;
    }
    for( int i = 0; i < pixel_count; i++ )
    {
        int index = sprite->palette_pixels[i];
        int colour = LC_TEXTURE_TRANSPARENT_RGB;
        if( index > 0 && index < pack->palette_length )
            colour = remap[index];
        rgb[i * 3 + 0] = (uint8_t)((colour >> 16) & 0xFF);
        rgb[i * 3 + 1] = (uint8_t)((colour >> 8) & 0xFF);
        rgb[i * 3 + 2] = (uint8_t)(colour & 0xFF);
    }

    free(used);
    free(counts);
    free(remap);
    RSCache_Dat2SpritePackFree(pack);

    size_t png_size = 0;
    void* png = tdefl_write_image_to_png_file_in_memory(rgb, size, size, 3, &png_size);
    free(rgb);
    if( !png || png_size == 0 )
    {
        free(png);
        lc_out_warn(ctx->out, "texture %d: PNG encode failed", src_texture_id);
        return lc_texture_refuse(ctx, src_texture_id);
    }

    int lc_id = lc_pack_alloc(&ctx->packs->packs[LC_PACK_TEXTURE], name);
    if( lc_id < 0 )
    {
        free(png);
        return -1;
    }
    if( lc_id > LC_MAX_TEXTURE_ID )
        lc_out_warn(
            ctx->out,
            "texture %s got id %d; a .flo stores its texture in one byte and cannot name it",
            name,
            lc_id);

    /* Flat, not under the area prefix: LostCity's packer reads
     * `<content>/textures/<name>.png` by name, with no subdirectory search. */
    char rel[512];
    snprintf(rel, sizeof(rel), "textures/%s.png", name);
    int ok = lc_out_write_file(ctx->out, rel, png, png_size);
    free(png);
    if( !ok )
        return -1;

    lc_id_map_put(&ctx->texture_map, src_texture_id, lc_id);
    return lc_id;
}
