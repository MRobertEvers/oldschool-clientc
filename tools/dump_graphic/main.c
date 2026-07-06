/*
 * Human-readable dump of dat1/dat2 cache graphics (sprites).
 *
 * Usage:
 *   dump_graphic <cache_dir> --graphic 675
 *   dump_graphic <cache_dir> --graphic 675 --frame 0 --bmp out.bmp
 *   dump_graphic <cache_dir> --graphic 675 --json
 *   dump_graphic <cache_dir> --dat1 --ref wornicons,3
 *
 * Cache format is auto-detected unless --dat1/--dat2 is set.
 * Dat2 uses sprite archive ids (interface graphic= fields). Dat1 uses --ref.
 */

#include "bmp.h"
#include "osrs/rscache/dat1a/dat1a_configs_dat.h"
#include "osrs/rscache/dat1a/dat1a_pix32.h"
#include "osrs/rscache/dat1a/dat1a_pix8.h"
#include "osrs/rscache/dat1disk/dat1disk.h"
#include "osrs/rscache/dat2a/dat2a_sprites.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "osrs/rscache/shared/shared_file_list.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum DumpGraphicCacheMode
{
    DUMP_GRAPHIC_CACHE_DAT1 = 0,
    DUMP_GRAPHIC_CACHE_DAT2 = 1
};

static int
detect_cache_mode(char const* cache_dir)
{
    if( !cache_dir )
        return -1;

    char path[1024];
    snprintf(path, sizeof(path), "%s/main_file_cache.dat2", cache_dir);
    FILE* fp = fopen(path, "rb");
    if( fp )
    {
        fclose(fp);
        return DUMP_GRAPHIC_CACHE_DAT2;
    }

    snprintf(path, sizeof(path), "%s/main_file_cache.dat", cache_dir);
    fp = fopen(path, "rb");
    if( fp )
    {
        fclose(fp);
        return DUMP_GRAPHIC_CACHE_DAT1;
    }

    return -1;
}

static void
json_escape(
    FILE* fp,
    char const* s)
{
    if( !s )
        return;
    for( char const* p = s; *p; p++ )
    {
        if( *p == '"' || *p == '\\' )
            fputc('\\', fp);
        if( *p == '\n' )
            fputs("\\n", fp);
        else if( *p == '\r' )
            fputs("\\r", fp);
        else if( *p == '\t' )
            fputs("\\t", fp);
        else
            fputc(*p, fp);
    }
}

static int
dat1_parse_sprite_ref(
    char const* sprite_ref,
    char* filename_out,
    size_t filename_cap,
    int* frame_out)
{
    if( !sprite_ref || !sprite_ref[0] || !filename_out || !frame_out )
        return -1;

    *frame_out = 0;

    if( sscanf(sprite_ref, "%255[^,],%d", filename_out, frame_out) == 2 )
        goto ensure_dat;

    {
        char name_buf[256];
        char index_buf[32];
        if( sscanf(sprite_ref, "%255[^[][%31[^]]", name_buf, index_buf) == 2 )
        {
            strncpy(filename_out, name_buf, filename_cap - 1);
            filename_out[filename_cap - 1] = '\0';
            *frame_out = atoi(index_buf);
            goto ensure_dat;
        }
    }

    strncpy(filename_out, sprite_ref, filename_cap - 1);
    filename_out[filename_cap - 1] = '\0';

ensure_dat:
{
    size_t len = strlen(filename_out);
    if( len + 5 <= filename_cap && (len < 4 || strcmp(filename_out + len - 4, ".dat") != 0) )
    {
        memcpy(filename_out + len, ".dat", 4);
        filename_out[len + 4] = '\0';
    }
}
    return 0;
}

static struct RSCacheShared_FileListDat*
dat1_load_media_jagfile(char const* cache_dir)
{
    struct RSCacheDat1Disk* disk = RSCacheDat1Disk_NewFromDirectory(cache_dir);
    if( !disk )
        return NULL;

    struct RSCacheDat1Disk_Archive* arc =
        RSCacheDat1Disk_ArchiveNewLoad(disk, 0, RSCacheDat1A_ConfigKind_Media2dGraphics);
    if( !arc )
    {
        RSCacheDat1Disk_Free(disk);
        return NULL;
    }

    struct RSCacheShared_FileListDat* fl = RSCacheShared_FileListDatNewFromCacheDatArchive(arc);
    RSCacheDat1Disk_ArchiveFree(arc);
    RSCacheDat1Disk_Free(disk);
    return fl;
}

static int
dat1_load_sprite(
    char const* cache_dir,
    char const* sprite_ref,
    int frame,
    struct RSCacheDat1A_Pix32** out_pix32,
    struct RSCacheDat1A_Pix8Palette** out_pix8,
    char* format_out,
    size_t format_cap)
{
    *out_pix32 = NULL;
    *out_pix8 = NULL;
    if( format_out && format_cap > 0 )
        format_out[0] = '\0';

    char data_name[256];
    if( dat1_parse_sprite_ref(sprite_ref, data_name, sizeof(data_name), &frame) != 0 )
        return -1;

    struct RSCacheShared_FileListDat* fl = dat1_load_media_jagfile(cache_dir);
    if( !fl )
        return -1;

    int data_idx = RSCacheShared_FileListDatFindFileByName(fl, data_name);
    int index_idx = RSCacheShared_FileListDatFindFileByName(fl, "index.dat");
    if( data_idx < 0 || index_idx < 0 )
    {
        RSCacheShared_FileListDatFree(fl);
        return -1;
    }

    struct RSCacheDat1A_Pix32* pix32 = RSCacheDat1A_Pix32New(
        fl->files[data_idx],
        fl->file_sizes[data_idx],
        fl->files[index_idx],
        fl->file_sizes[index_idx],
        frame);
    if( pix32 )
    {
        if( format_out && format_cap > 0 )
            strncpy(format_out, "pix32", format_cap - 1);
        *out_pix32 = pix32;
        RSCacheShared_FileListDatFree(fl);
        return 0;
    }

    struct RSCacheDat1A_Pix8Palette* pix8 = RSCacheDat1A_Pix8PaletteNew(
        fl->files[data_idx],
        fl->file_sizes[data_idx],
        fl->files[index_idx],
        fl->file_sizes[index_idx],
        frame);
    RSCacheShared_FileListDatFree(fl);
    if( !pix8 )
        return -1;

    if( format_out && format_cap > 0 )
        strncpy(format_out, "pix8", format_cap - 1);
    *out_pix8 = pix8;
    return 0;
}

static int*
pix8_to_argb(struct RSCacheDat1A_Pix8Palette const* pix8)
{
    if( !pix8 || !pix8->pixels || !pix8->palette || pix8->width <= 0 || pix8->height <= 0 )
        return NULL;

    int n = pix8->width * pix8->height;
    int* out = calloc((size_t)n, sizeof(int));
    if( !out )
        return NULL;

    for( int i = 0; i < n; i++ )
    {
        int idx = (int)(unsigned char)pix8->pixels[i];
        int rgb = (idx >= 0 && idx < pix8->palette_count) ? pix8->palette[idx] : 0;
        uint8_t alpha = idx == 0 ? 0 : 0xFF;
        out[i] = ((int)alpha << 24) | (rgb & 0xFFFFFF);
    }
    return out;
}

static void
print_dat2_frame(
    struct RSCacheDat2A_SpritePack const* pack,
    int frame,
    FILE* fp)
{
    if( !pack || frame < 0 || frame >= pack->count )
        return;

    struct RSCacheDat2A_Sprite const* s = &pack->sprites[frame];
    int has_alpha = 0;
    if( s->pixel_alphas && s->crop_width > 0 && s->crop_height > 0 )
    {
        int n = s->crop_width * s->crop_height;
        for( int i = 0; i < n; i++ )
        {
            if( s->pixel_alphas[i] != 0xFF )
            {
                has_alpha = 1;
                break;
            }
        }
    }

    fprintf(
        fp,
        "frame[%d]  memory=%dx%d  crop=%dx%d  offset=(%d,%d)  alpha=%s\n",
        frame,
        s->width,
        s->height,
        s->crop_width,
        s->crop_height,
        s->offset_x,
        s->offset_y,
        has_alpha ? "yes" : "no");
}

static void
print_dat2_pack(
    int graphic_id,
    struct RSCacheDat2A_SpritePack const* pack,
    int frame_filter,
    FILE* fp)
{
    fprintf(
        fp,
        "Graphic %d  frames=%d  palette=%d\n",
        graphic_id,
        pack ? pack->count : 0,
        pack ? pack->palette_length : 0);

    if( !pack || pack->count <= 0 )
        return;

    if( frame_filter >= 0 )
        print_dat2_frame(pack, frame_filter, fp);
    else
    {
        for( int i = 0; i < pack->count; i++ )
            print_dat2_frame(pack, i, fp);
    }
}

static void
print_dat1_sprite(
    char const* ref,
    int frame,
    char const* format,
    struct RSCacheDat1A_Pix32 const* pix32,
    struct RSCacheDat1A_Pix8Palette const* pix8,
    FILE* fp)
{
    fprintf(fp, "Graphic ref='%s'  frame=%d  format=%s\n", ref, frame, format ? format : "?");

    if( pix32 )
    {
        fprintf(
            fp,
            "  draw=%dx%d  stride=%dx%d  crop=(%d,%d)\n",
            pix32->draw_width,
            pix32->draw_height,
            pix32->stride_x,
            pix32->stride_y,
            pix32->crop_x,
            pix32->crop_y);
    }
    else if( pix8 )
    {
        fprintf(
            fp,
            "  draw=%dx%d  crop_box=%dx%d  crop=(%d,%d)  palette=%d\n",
            pix8->width,
            pix8->height,
            pix8->crop_width,
            pix8->crop_height,
            pix8->crop_x,
            pix8->crop_y,
            pix8->palette_count);
    }
}

static void
print_json_dat2(
    int graphic_id,
    struct RSCacheDat2A_SpritePack const* pack,
    int frame_filter,
    FILE* fp)
{
    fprintf(fp, "{\n  \"graphic\": %d,\n  \"frames\": [\n", graphic_id);
    int first = 1;
    int start = frame_filter >= 0 ? frame_filter : 0;
    int end = frame_filter >= 0 ? frame_filter + 1 : pack->count;
    for( int i = start; i < end; i++ )
    {
        struct RSCacheDat2A_Sprite const* s = &pack->sprites[i];
        if( !first )
            fprintf(fp, ",\n");
        first = 0;
        fprintf(
            fp,
            "    {\"frame\": %d, \"memory_w\": %d, \"memory_h\": %d, "
            "\"crop_w\": %d, \"crop_h\": %d, \"offset_x\": %d, \"offset_y\": %d}",
            i,
            s->width,
            s->height,
            s->crop_width,
            s->crop_height,
            s->offset_x,
            s->offset_y);
    }
    fprintf(fp, "\n  ],\n  \"palette_length\": %d\n}\n", pack->palette_length);
}

static void
print_json_dat1(
    char const* ref,
    int frame,
    char const* format,
    struct RSCacheDat1A_Pix32 const* pix32,
    struct RSCacheDat1A_Pix8Palette const* pix8,
    FILE* fp)
{
    fprintf(fp, "{\n  \"ref\": \"");
    json_escape(fp, ref);
    fprintf(fp, "\",\n  \"frame\": %d,\n  \"format\": \"%s\"", frame, format ? format : "");
    if( pix32 )
    {
        fprintf(
            fp,
            ",\n  \"draw_w\": %d, \"draw_h\": %d, \"stride_w\": %d, \"stride_h\": %d, "
            "\"crop_x\": %d, \"crop_y\": %d",
            pix32->draw_width,
            pix32->draw_height,
            pix32->stride_x,
            pix32->stride_y,
            pix32->crop_x,
            pix32->crop_y);
    }
    else if( pix8 )
    {
        fprintf(
            fp,
            ",\n  \"draw_w\": %d, \"draw_h\": %d, \"crop_w\": %d, \"crop_h\": %d, "
            "\"crop_x\": %d, \"crop_y\": %d, \"palette_length\": %d",
            pix8->width,
            pix8->height,
            pix8->crop_width,
            pix8->crop_height,
            pix8->crop_x,
            pix8->crop_y,
            pix8->palette_count);
    }
    fprintf(fp, "\n}\n");
}

static int
export_dat2_bmp(
    struct RSCacheDat2A_SpritePack const* pack,
    int frame,
    char const* path)
{
    if( !pack || frame < 0 || frame >= pack->count || !path )
        return -1;

    struct RSCacheDat2A_Sprite* sprite = &pack->sprites[frame];
    int* pixels = RSCacheDat2A_SpriteGetPixels(sprite, pack->palette, 1);
    if( !pixels )
        return -1;

    printf("writing bmp to %s: %dx%d\n", path, sprite->width, sprite->height);
    bmp_write_file(path, pixels, sprite->width, sprite->height);
    free(pixels);
    return 0;
}

static int
export_dat1_bmp(
    struct RSCacheDat1A_Pix32 const* pix32,
    struct RSCacheDat1A_Pix8Palette const* pix8,
    char const* path)
{
    if( !path )
        return -1;

    if( pix32 && pix32->pixels )
    {
        printf("writing bmp to %s: %dx%d\n", path, pix32->stride_x, pix32->stride_y);
        bmp_write_file(path, pix32->pixels, pix32->stride_x, pix32->stride_y);
        return 0;
    }

    if( pix8 )
    {
        int* pixels = pix8_to_argb(pix8);
        if( !pixels )
            return -1;
        printf("writing bmp to %s: %dx%d\n", path, pix8->width, pix8->height);
        bmp_write_file(path, pixels, pix8->width, pix8->height);

        free(pixels);
        return 0;
    }

    return -1;
}

static void
usage(void)
{
    fprintf(
        stderr,
        "usage: dump_graphic <cache_dir> [--dat1 | --dat2]\n"
        "       (--graphic N | --ref sprite_ref) [--frame N] [--json] [--bmp path]\n");
}

int
main(
    int argc,
    char** argv)
{
    char const* cache_dir = NULL;
    int graphic_id = -1;
    char const* sprite_ref = NULL;
    int frame = -1;
    int json_mode = 0;
    char const* bmp_path = NULL;
    int cache_mode = -1;
    char dat1_ref_buf[64];

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--graphic") == 0 && i + 1 < argc )
            graphic_id = atoi(argv[++i]);
        else if( strcmp(argv[i], "--id") == 0 && i + 1 < argc )
            graphic_id = atoi(argv[++i]);
        else if( strcmp(argv[i], "--ref") == 0 && i + 1 < argc )
            sprite_ref = argv[++i];
        else if( strcmp(argv[i], "--frame") == 0 && i + 1 < argc )
            frame = atoi(argv[++i]);
        else if( strcmp(argv[i], "--json") == 0 )
            json_mode = 1;
        else if( strcmp(argv[i], "--bmp") == 0 && i + 1 < argc )
            bmp_path = argv[++i];
        else if( strcmp(argv[i], "--dat1") == 0 )
            cache_mode = DUMP_GRAPHIC_CACHE_DAT1;
        else if( strcmp(argv[i], "--dat2") == 0 )
            cache_mode = DUMP_GRAPHIC_CACHE_DAT2;
        else if( argv[i][0] != '-' && !cache_dir )
            cache_dir = argv[i];
    }

    if( !cache_dir )
    {
        usage();
        return 1;
    }

    if( cache_mode < 0 )
        cache_mode = detect_cache_mode(cache_dir);
    if( cache_mode < 0 )
    {
        fprintf(stderr, "failed to detect cache format in: %s\n", cache_dir);
        return 1;
    }

    if( cache_mode == DUMP_GRAPHIC_CACHE_DAT2 )
    {
        if( graphic_id < 0 )
        {
            fprintf(stderr, "dat2 cache requires --graphic N\n");
            usage();
            return 1;
        }

        struct RSCacheDat2Disk* cache = RSCacheDat2Disk_NewFromDirectory(cache_dir);
        if( !cache )
        {
            fprintf(stderr, "failed to open dat2 cache: %s\n", cache_dir);
            return 1;
        }

        struct RSCacheDat2A_SpritePack* pack =
            RSCacheDat2A_SpritePackNewFromCache(cache, graphic_id);
        RSCacheDat2Disk_Free(cache);
        if( !pack )
        {
            fprintf(stderr, "failed to load graphic %d\n", graphic_id);
            return 1;
        }

        if( json_mode )
            print_json_dat2(graphic_id, pack, frame, stdout);
        else
            print_dat2_pack(graphic_id, pack, frame, stdout);

        if( bmp_path )
        {
            int export_frame = frame >= 0 ? frame : 0;
            if( export_frame >= pack->count )
            {
                fprintf(stderr, "frame %d out of range (count=%d)\n", export_frame, pack->count);
                RSCacheDat2A_SpritePackFree(pack);
                return 1;
            }
            if( export_dat2_bmp(pack, export_frame, bmp_path) != 0 )
                fprintf(stderr, "failed to write bmp: %s\n", bmp_path);
        }

        RSCacheDat2A_SpritePackFree(pack);
        return 0;
    }

    if( !sprite_ref )
    {
        if( graphic_id < 0 )
        {
            fprintf(stderr, "dat1 cache requires --ref sprite_ref\n");
            usage();
            return 1;
        }

        snprintf(dat1_ref_buf, sizeof(dat1_ref_buf), "%d", graphic_id);
        sprite_ref = dat1_ref_buf;
    }

    int dat1_frame = frame >= 0 ? frame : 0;
    char format[16];
    struct RSCacheDat1A_Pix32* pix32 = NULL;
    struct RSCacheDat1A_Pix8Palette* pix8 = NULL;
    if( dat1_load_sprite(
            cache_dir, sprite_ref, dat1_frame, &pix32, &pix8, format, sizeof(format)) != 0 )
    {
        fprintf(stderr, "failed to load dat1 graphic ref='%s' frame=%d\n", sprite_ref, dat1_frame);
        return 1;
    }

    if( json_mode )
        print_json_dat1(sprite_ref, dat1_frame, format, pix32, pix8, stdout);
    else
        print_dat1_sprite(sprite_ref, dat1_frame, format, pix32, pix8, stdout);

    if( bmp_path && export_dat1_bmp(pix32, pix8, bmp_path) != 0 )
        fprintf(stderr, "failed to write bmp: %s\n", bmp_path);

    RSCacheDat1A_Pix32Free(pix32);
    RSCacheDat1A_Pix8PaletteFree(pix8);
    return 0;
}
