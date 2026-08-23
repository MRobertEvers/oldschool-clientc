/*
 * Cut named sprites out of a cache and write them as PNG.
 *
 * The authoring tool behind `script/plugins/assets/layout-*`: a layout plugin
 * ships its own art, so the art has to leave the cache once, by hand, and
 * become ordinary files. That is the whole reason this exists -- see
 * `plugin/plugins/gameframe.c` for why a plugin may not name a graphic by
 * cache id.
 *
 * It is a TOOL and not a client path: nothing in the client reads a PNG out of
 * a cache. Runs are recorded in each asset directory's own SOURCES.sh, so
 * the asset set can be regenerated from the same caches rather than being a
 * folder of pictures with no provenance.
 *
 * Usage:
 *   dump_sprites --dat2 --rev NAME <cache_dir> --out <dir> <spec>...
 *   dump_sprites --dat1 <cache_dir> --out <dir> <spec>...
 *
 * A spec is `<name>=<source>[:<frames>]`, where <source> is a sprite id on dat2
 * and a media-jagfile filename on dat1. <frames> is one atlas index, an
 * inclusive `<a>-<b>` range, or `*` for every frame the source declares. One
 * frame writes `<name>.png`; several write `<name>_<i>.png`.
 *
 * `*` is dat2-only, and that is a property of the FORMATS rather than a
 * shortcut not taken: a dat2 pack states how many sprites it holds, and a dat1
 * image does not -- its index entry is read per frame, and reading one past the
 * end returns a decode rather than a failure. Walking a dat1 atlas until it
 * stops therefore produces one plausible file and then a 25000-pixel-tall
 * bitmap of whatever followed it in the archive. Say the range.
 */

#include <rscache.h>

#include "engine/torirs_sprite_from_rscache.h"
#include "engine/torirs_types.h"

#include <miniz.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dat1disk.h"
#include "dat2disk.h"
#include "filelist.h"

static void
usage(char const* argv0)
{
    fprintf(
        stderr,
        "Usage:\n"
        "  %s --dat2 --rev NAME <cache_dir> --out <dir> <name>=<sprite_id>[:<frame>|:*]...\n"
        "  %s --dat1 <cache_dir> --out <dir> <name>=<file.dat>[:<frame>|:*]...\n",
        argv0,
        argv0);
}

/*
 * One frame to `<dir>/<stem>.png`.
 *
 * RGBA and not RGB: a cut-out is what interface art is, and the alpha channel
 * is the only thing that makes the stone frame's rounded corners sit over a
 * viewport instead of over a black box.
 */
static int
write_png(
    char const* dir,
    char const* stem,
    struct ToriRS_SpriteFrame const* frame)
{
    char path[512];
    uint8_t* rgba;
    void* png;
    size_t png_size = 0;
    FILE* f;

    assert(dir);
    assert(stem);
    assert(frame);
    if( frame->width <= 0 || frame->height <= 0 || !frame->pixels_argb )
    {
        fprintf(stderr, "  %s: empty frame\n", stem);
        return 0;
    }

    rgba = malloc((size_t)frame->width * (size_t)frame->height * 4);
    assert(rgba);
    for( int i = 0; i < frame->width * frame->height; i++ )
    {
        uint32_t argb = frame->pixels_argb[i];
        rgba[i * 4 + 0] = (uint8_t)((argb >> 16) & 0xff);
        rgba[i * 4 + 1] = (uint8_t)((argb >> 8) & 0xff);
        rgba[i * 4 + 2] = (uint8_t)(argb & 0xff);
        rgba[i * 4 + 3] = (uint8_t)((argb >> 24) & 0xff);
    }

    png = tdefl_write_image_to_png_file_in_memory_ex(
        rgba, frame->width, frame->height, 4, &png_size, 9, MZ_FALSE);
    free(rgba);
    assert(png);

    snprintf(path, sizeof(path), "%s/%s.png", dir, stem);
    f = fopen(path, "wb");
    if( !f )
    {
        fprintf(stderr, "  %s: cannot write %s\n", stem, path);
        mz_free(png);
        return 0;
    }
    fwrite(png, 1, png_size, f);
    fclose(f);
    mz_free(png);
    printf("  %-24s %3dx%-3d -> %s\n", stem, frame->width, frame->height, path);
    return 1;
}

static int
write_sprite(
    char const* dir,
    char const* name,
    struct ToriRS_Sprite* sprite,
    int first,
    int last,
    int all_frames)
{
    char stem[128];
    int written = 0;

    assert(dir);
    assert(name);
    if( !sprite || sprite->frame_count <= 0 )
    {
        fprintf(stderr, "  %s: no frames decoded\n", name);
        return 0;
    }

    if( all_frames )
    {
        first = 0;
        last = sprite->frame_count - 1;
    }
    if( first < 0 || last >= sprite->frame_count )
    {
        fprintf(stderr, "  %s: frames %d..%d of %d\n", name, first, last, sprite->frame_count);
        return 0;
    }
    /* One frame keeps the bare name: a spec that asked for a single picture
     * should not have to know it came out of an atlas. */
    if( first == last && !all_frames )
        return write_png(dir, name, &sprite->frames[first]);

    for( int i = first; i <= last; i++ )
    {
        snprintf(stem, sizeof(stem), "%s_%d", name, i);
        written += write_png(dir, stem, &sprite->frames[i]);
    }
    return written;
}

/* `<name>=<source>[:<frame>|:*]`, split in place into the caller's buffers. */
static int
parse_spec(
    char const* spec,
    char* out_name,
    int name_size,
    char* out_source,
    int source_size,
    int* out_frame,
    int* out_last,
    int* out_all)
{
    char const* eq;
    char const* colon;
    size_t name_len;
    size_t source_len;

    assert(spec);
    assert(out_name);
    assert(out_source);
    assert(out_frame);
    assert(out_last);
    assert(out_all);

    *out_frame = 0;
    *out_last = 0;
    *out_all = 0;

    eq = strchr(spec, '=');
    if( !eq )
        return 0;
    name_len = (size_t)(eq - spec);
    if( name_len == 0 || name_len >= (size_t)name_size )
        return 0;
    memcpy(out_name, spec, name_len);
    out_name[name_len] = '\0';

    colon = strchr(eq + 1, ':');
    source_len = colon ? (size_t)(colon - eq - 1) : strlen(eq + 1);
    if( source_len == 0 || source_len >= (size_t)source_size )
        return 0;
    memcpy(out_source, eq + 1, source_len);
    out_source[source_len] = '\0';

    if( colon )
    {
        if( colon[1] == '*' && colon[2] == '\0' )
        {
            *out_all = 1;
        }
        else
        {
            char const* dash = strchr(colon + 1, '-');
            *out_frame = atoi(colon + 1);
            *out_last = dash ? atoi(dash + 1) : *out_frame;
            if( *out_last < *out_frame )
                return 0;
        }
    }
    return 1;
}

static int
run_dat2(
    char const* cache_dir,
    char const* rev,
    char const* out_dir,
    char* const* specs,
    int spec_count)
{
    struct RSCache profile;
    struct RSCache_Dat2Disk* disk;
    int table;
    int written = 0;

    assert(cache_dir);
    assert(out_dir);
    if( !rev )
    {
        fprintf(stderr, "--dat2 needs --rev NAME (the sprite decode is era-gated)\n");
        return 0;
    }
    if( !RSCache_ProfileByName(rev, &profile) )
    {
        fprintf(stderr, "unknown --rev '%s'\n", rev);
        return 0;
    }

    disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    if( !disk )
    {
        fprintf(stderr, "cannot open dat2 cache at %s\n", cache_dir);
        return 0;
    }
    RSCache_Dat2DiskSetProfile(disk, &profile);
    table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_SPRITES);

    for( int i = 0; i < spec_count; i++ )
    {
        char name[128];
        char source[128];
        int frame;
        int last;
        int all;
        struct RSCache_Dat2DiskArchive* archive;
        struct ToriRS_Sprite* sprite;

        if( !parse_spec(
                specs[i], name, sizeof(name), source, sizeof(source), &frame, &last, &all) )
        {
            fprintf(stderr, "bad spec '%s'\n", specs[i]);
            continue;
        }
        archive = RSCache_Dat2DiskArchiveNewLoad(disk, table, atoi(source));
        if( !archive )
        {
            fprintf(stderr, "  %s: sprite %s not in this cache\n", name, source);
            continue;
        }
        /* Takes the archive, frees it either way. */
        sprite = ToriRS_SpriteFromDat2Archive(archive, atoi(source), &profile);
        written += write_sprite(out_dir, name, sprite, frame, last, all);
        ToriRS_SpriteFree(sprite);
    }

    RSCache_Dat2DiskFree(disk);
    return written;
}

static int
run_dat1(
    char const* cache_dir,
    char const* out_dir,
    char* const* specs,
    int spec_count)
{
    struct RSCache_Dat1Disk* disk;
    struct RSCache_Dat1DiskArchive* archive;
    struct RSCache_FileListDat* media;
    int written = 0;

    assert(cache_dir);
    assert(out_dir);

    disk = RSCache_Dat1DiskNewFromDirectory(cache_dir);
    if( !disk )
    {
        fprintf(stderr, "cannot open dat1 cache at %s\n", cache_dir);
        return 0;
    }
    archive = RSCache_Dat1DiskArchiveNewLoad(
        disk, RSCACHE_DAT1_DISK_TABLE_CONFIGS, RSCACHE_DAT1_CONFIG_MEDIA_2D);
    if( !archive )
    {
        fprintf(stderr, "no media archive in %s\n", cache_dir);
        RSCache_Dat1DiskFree(disk);
        return 0;
    }
    media = RSCache_FileListDatNewFromDecode(archive->data, archive->data_size);
    RSCache_Dat1DiskArchiveFree(archive);
    if( !media )
    {
        fprintf(stderr, "media archive would not decode\n");
        RSCache_Dat1DiskFree(disk);
        return 0;
    }

    for( int i = 0; i < spec_count; i++ )
    {
        char name[128];
        char source[128];
        int frame;
        int last;
        int all;
        struct ToriRS_Sprite* sprite;

        if( !parse_spec(
                specs[i], name, sizeof(name), source, sizeof(source), &frame, &last, &all) )
        {
            fprintf(stderr, "bad spec '%s'\n", specs[i]);
            continue;
        }
        if( all )
        {
            fprintf(stderr, "  %s: dat1 needs a frame range, not '*'\n", name);
            continue;
        }
        /* Frames [0..last] are decoded and [first..last] written, because the
         * jagfile decode is indexed from 0 and there is nothing to seek. */
        sprite = ToriRS_SpriteFromDat1Jagfile(media, "pix8", source, "index.dat", 0, last + 1);
        if( !sprite )
        {
            /* pix32 is the other authored format; component graphics do not
             * record which one they are. */
            sprite = ToriRS_SpriteFromDat1Jagfile(media, "pix32", source, "index.dat", 0, last + 1);
        }
        written += write_sprite(out_dir, name, sprite, frame, last, 0);
        ToriRS_SpriteFree(sprite);
    }

    RSCache_FileListDatFree(media);
    RSCache_Dat1DiskFree(disk);
    return written;
}

int
main(int argc, char** argv)
{
    char const* cache_dir = NULL;
    char const* out_dir = NULL;
    char const* rev = NULL;
    int dat1 = 0;
    int dat2 = 0;
    int written;
    int spec_start;

    for( spec_start = 1; spec_start < argc; spec_start++ )
    {
        char const* a = argv[spec_start];
        if( strcmp(a, "--dat1") == 0 )
            dat1 = 1;
        else if( strcmp(a, "--dat2") == 0 )
            dat2 = 1;
        else if( strcmp(a, "--rev") == 0 && spec_start + 1 < argc )
            rev = argv[++spec_start];
        else if( strcmp(a, "--out") == 0 && spec_start + 1 < argc )
            out_dir = argv[++spec_start];
        else if( strncmp(a, "--", 2) == 0 )
        {
            usage(argv[0]);
            return 1;
        }
        else if( !cache_dir )
            cache_dir = a;
        else
            break; /* first spec */
    }

    if( !cache_dir || !out_dir || (dat1 == dat2) || spec_start >= argc )
    {
        usage(argv[0]);
        return 1;
    }

    printf("%s -> %s\n", cache_dir, out_dir);
    written = dat1 ? run_dat1(cache_dir, out_dir, &argv[spec_start], argc - spec_start)
                   : run_dat2(cache_dir, rev, out_dir, &argv[spec_start], argc - spec_start);
    printf("%d file(s)\n", written);
    return written > 0 ? 0 : 1;
}
