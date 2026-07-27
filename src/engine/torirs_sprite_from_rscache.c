#include "engine/torirs_sprite_from_rscache.h"

#include "engine/torirs_types.h"

#include <assert.h>
#include <rscache.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct ToriRS_SpriteFrame
sprite_frame_from_dat2_sprite(
    struct RSCache_Dat2SpritePack const* pack,
    int frame_index)
{
    struct ToriRS_SpriteFrame frame;
    struct RSCache_Dat2Sprite* def;
    int* px;

    memset(&frame, 0, sizeof(frame));
    assert(pack);
    assert(frame_index >= 0 && frame_index < pack->count);

    def = &pack->sprites[frame_index];
    px = RSCache_Dat2SpriteGetPixels(def, pack->palette, 0);
    if( !px )
        return frame;

    frame.pixels_argb = (uint32_t*)px;
    frame.width = def->crop_width > 0 ? def->crop_width : def->width;
    frame.height = def->crop_height > 0 ? def->crop_height : def->height;
    frame.crop_x = def->offset_x;
    frame.crop_y = def->offset_y;
    frame.crop_width = frame.width;
    frame.crop_height = frame.height;
    return frame;
}

struct ToriRS_Sprite*
ToriRS_SpriteFromDat2Pack(
    struct RSCache_Dat2SpritePack* pack,
    int sprite_id)
{
    struct ToriRS_Sprite* sprite;
    int i;
    int loaded;

    assert(pack);
    if( pack->count <= 0 )
        return NULL;

    sprite = calloc(1, sizeof(*sprite));
    if( !sprite )
        return NULL;

    sprite->frames = calloc((size_t)pack->count, sizeof(*sprite->frames));
    if( !sprite->frames )
    {
        free(sprite);
        return NULL;
    }
    sprite->frame_count = pack->count;
    snprintf(sprite->name, sizeof(sprite->name), "spr:%d", sprite_id);

    loaded = 0;
    for( i = 0; i < pack->count; i++ )
    {
        sprite->frames[i] = sprite_frame_from_dat2_sprite(pack, i);
        if( sprite->frames[i].pixels_argb )
            loaded++;
    }

    if( loaded <= 0 )
    {
        ToriRS_SpriteFree(sprite);
        return NULL;
    }
    return sprite;
}

/* Trimmed pix8 pixels stay trimmed: crop_x/y are the draw offsets, crop_width/height
 * the full canvas (Client.ts Pix8.draw blits at (x + crop_x, y + crop_y)). */
static struct ToriRS_SpriteFrame
sprite_frame_from_dat1_pix8(struct RSCache_Dat1Pix8 const* pix8)
{
    struct ToriRS_SpriteFrame frame;
    uint32_t* argb;
    int n;

    memset(&frame, 0, sizeof(frame));
    assert(pix8);
    if( pix8->width <= 0 || pix8->height <= 0 )
        return frame;

    n = pix8->width * pix8->height;
    argb = calloc((size_t)n, sizeof(uint32_t));
    if( !argb )
        return frame;

    for( int i = 0; i < n; i++ )
    {
        int palette_index = pix8->pixels[i];
        if( palette_index <= 0 || palette_index >= pix8->palette_count )
            continue;
        argb[i] = 0xFF000000u | (uint32_t)pix8->palette[palette_index];
    }

    frame.pixels_argb = argb;
    frame.width = pix8->width;
    frame.height = pix8->height;
    frame.crop_x = pix8->crop_x;
    frame.crop_y = pix8->crop_y;
    frame.crop_width = pix8->crop_width;
    frame.crop_height = pix8->crop_height;
    return frame;
}

/* Pix32 expands to the full draw canvas with the trim offset baked in (mirrors
 * ToriDraw_SpriteNewFromPix32) so INI crops address canvas coordinates. */
static struct ToriRS_SpriteFrame
sprite_frame_from_dat1_pix32(struct RSCache_Dat1Pix32 const* pix32)
{
    struct ToriRS_SpriteFrame frame;
    uint32_t* argb;
    int copy_w;
    int copy_h;

    memset(&frame, 0, sizeof(frame));
    assert(pix32);
    if( pix32->draw_width <= 0 || pix32->draw_height <= 0 )
        return frame;

    argb = calloc((size_t)pix32->draw_width * (size_t)pix32->draw_height, sizeof(uint32_t));
    if( !argb )
        return frame;

    copy_w = pix32->stride_x > pix32->draw_width ? pix32->draw_width : pix32->stride_x;
    copy_h = pix32->stride_y > pix32->draw_height ? pix32->draw_height : pix32->stride_y;

    for( int y = 0; y < copy_h; y++ )
    {
        int write_y = y + pix32->crop_y;
        if( write_y < 0 || write_y >= pix32->draw_height )
            continue;
        for( int x = 0; x < copy_w; x++ )
        {
            int write_x = x + pix32->crop_x;
            int rgb;
            if( write_x < 0 || write_x >= pix32->draw_width )
                continue;
            rgb = pix32->pixels[x + y * pix32->stride_x];
            if( rgb == 0 )
                continue;
            argb[write_x + write_y * pix32->draw_width] = 0xFF000000u | (uint32_t)rgb;
        }
    }

    frame.pixels_argb = argb;
    frame.width = pix32->draw_width;
    frame.height = pix32->draw_height;
    frame.crop_width = pix32->draw_width;
    frame.crop_height = pix32->draw_height;
    return frame;
}

static struct ToriRS_SpriteFrame
sprite_frame_from_dat1_jagfile(
    struct RSCache_FileListDat* jagfile,
    int data_file_idx,
    int index_file_idx,
    char const* format,
    int frame_idx)
{
    struct ToriRS_SpriteFrame frame;
    void* data = jagfile->files[data_file_idx];
    int data_size = jagfile->file_sizes[data_file_idx];
    void* index_data = jagfile->files[index_file_idx];
    int index_size = jagfile->file_sizes[index_file_idx];

    memset(&frame, 0, sizeof(frame));
    if( strcmp(format, "pix32") == 0 )
    {
        struct RSCache_Dat1Pix32* pix32 =
            RSCache_Dat1Pix32New(data, data_size, index_data, index_size, frame_idx);
        if( !pix32 )
            return frame;
        frame = sprite_frame_from_dat1_pix32(pix32);
        RSCache_Dat1Pix32Free(pix32);
    }
    else
    {
        struct RSCache_Dat1Pix8* pix8 =
            RSCache_Dat1Pix8New(data, data_size, index_data, index_size, frame_idx);
        if( !pix8 )
            return frame;
        frame = sprite_frame_from_dat1_pix8(pix8);
        RSCache_Dat1Pix8Free(pix8);
    }
    return frame;
}

struct ToriRS_Sprite*
ToriRS_SpriteFromDat1Jagfile(
    struct RSCache_FileListDat* media_jagfile,
    char const* format,
    char const* data_filename,
    char const* index_filename,
    int atlas_index,
    int atlas_count)
{
    struct ToriRS_Sprite* sprite;
    int data_file_idx;
    int index_file_idx;
    int frame_count;
    int loaded;

    assert(media_jagfile);
    assert(format);
    assert(data_filename);
    assert(index_filename);

    data_file_idx = RSCache_FileListDatFindFileByName(media_jagfile, (char*)data_filename);
    index_file_idx = RSCache_FileListDatFindFileByName(media_jagfile, (char*)index_filename);
    if( data_file_idx == -1 || index_file_idx == -1 )
    {
        fprintf(
            stderr,
            "ToriRS_SpriteFromDat1Jagfile: missing %s/%s in media jagfile\n",
            data_filename,
            index_filename);
        return NULL;
    }

    frame_count = atlas_count > 0 ? atlas_count : 1;

    sprite = calloc(1, sizeof(*sprite));
    if( !sprite )
        return NULL;
    sprite->frames = calloc((size_t)frame_count, sizeof(*sprite->frames));
    if( !sprite->frames )
    {
        free(sprite);
        return NULL;
    }
    sprite->frame_count = frame_count;
    strncpy(sprite->name, data_filename, sizeof(sprite->name) - 1);

    loaded = 0;
    for( int i = 0; i < frame_count; i++ )
    {
        int frame_idx = atlas_count > 0 ? i : atlas_index;
        sprite->frames[i] = sprite_frame_from_dat1_jagfile(
            media_jagfile, data_file_idx, index_file_idx, format, frame_idx);
        if( sprite->frames[i].pixels_argb )
            loaded++;
        else
            break;
    }

    if( loaded <= 0 )
    {
        ToriRS_SpriteFree(sprite);
        return NULL;
    }
    sprite->frame_count = loaded;
    return sprite;
}

struct ToriRS_Sprite*
ToriRS_SpriteFromDat1Ref(
    struct RSCache_FileListDat* media_jagfile,
    char const* ref)
{
    char name[64];
    char data_filename[80];
    struct ToriRS_Sprite* sprite;
    int frame_idx = 0;
    size_t stem_len;
    char const* sep;

    assert(media_jagfile);
    assert(ref);
    if( ref[0] == '\0' )
        return NULL;

    sep = strchr(ref, ',');
    if( !sep )
        sep = strchr(ref, '[');
    if( sep )
    {
        stem_len = (size_t)(sep - ref);
        frame_idx = atoi(sep + 1);
    }
    else
    {
        stem_len = strlen(ref);
    }
    if( stem_len >= sizeof(name) )
        stem_len = sizeof(name) - 1;
    memcpy(name, ref, stem_len);
    name[stem_len] = '\0';

    if( strstr(name, ".dat") )
        snprintf(data_filename, sizeof(data_filename), "%s", name);
    else
        snprintf(data_filename, sizeof(data_filename), "%s.dat", name);

    sprite = ToriRS_SpriteFromDat1Jagfile(
        media_jagfile, "pix32", data_filename, "index.dat", frame_idx, 0);
    if( !sprite )
        sprite = ToriRS_SpriteFromDat1Jagfile(
            media_jagfile, "pix8", data_filename, "index.dat", frame_idx, 0);
    if( sprite )
    {
        strncpy(sprite->name, ref, sizeof(sprite->name) - 1);
        sprite->name[sizeof(sprite->name) - 1] = '\0';
    }
    return sprite;
}

void
ToriRS_SpriteApplyCrop(
    struct ToriRS_Sprite* sprite,
    int crop_x,
    int crop_y,
    int crop_width,
    int crop_height)
{
    assert(sprite);
    if( crop_width <= 0 || crop_height <= 0 )
        return;

    for( int i = 0; i < sprite->frame_count; i++ )
    {
        struct ToriRS_SpriteFrame* frame = &sprite->frames[i];
        uint32_t* cropped;
        int ox = crop_x < 0 ? 0 : crop_x;
        int oy = crop_y < 0 ? 0 : crop_y;
        int w = crop_width;
        int h = crop_height;

        if( !frame->pixels_argb || ox >= frame->width || oy >= frame->height )
            continue;
        if( ox + w > frame->width )
            w = frame->width - ox;
        if( oy + h > frame->height )
            h = frame->height - oy;
        if( w <= 0 || h <= 0 )
            continue;

        cropped = calloc((size_t)w * (size_t)h, sizeof(uint32_t));
        if( !cropped )
            continue;
        for( int y = 0; y < h; y++ )
            memcpy(
                cropped + (size_t)y * w,
                frame->pixels_argb + (size_t)(oy + y) * frame->width + ox,
                (size_t)w * sizeof(uint32_t));

        free(frame->pixels_argb);
        frame->pixels_argb = cropped;
        frame->width = w;
        frame->height = h;
        frame->crop_x = 0;
        frame->crop_y = 0;
        frame->crop_width = w;
        frame->crop_height = h;
    }
}

void
ToriRS_SpriteApplyTransform(
    struct ToriRS_Sprite* sprite,
    char const* transform)
{
    int flip_h;
    int flip_v;

    assert(sprite);
    assert(transform);
    flip_h = strcmp(transform, "flip_h") == 0;
    flip_v = strcmp(transform, "flip_v") == 0;
    if( !flip_h && !flip_v )
        return;

    for( int i = 0; i < sprite->frame_count; i++ )
    {
        struct ToriRS_SpriteFrame* frame = &sprite->frames[i];
        if( !frame->pixels_argb )
            continue;

        if( flip_h )
        {
            for( int y = 0; y < frame->height; y++ )
            {
                uint32_t* row = frame->pixels_argb + (size_t)y * frame->width;
                for( int x = 0; x < frame->width / 2; x++ )
                {
                    uint32_t tmp = row[x];
                    row[x] = row[frame->width - 1 - x];
                    row[frame->width - 1 - x] = tmp;
                }
            }
            if( frame->crop_width > frame->width )
                frame->crop_x = frame->crop_width - frame->width - frame->crop_x;
        }
        else
        {
            for( int y = 0; y < frame->height / 2; y++ )
            {
                uint32_t* top = frame->pixels_argb + (size_t)y * frame->width;
                uint32_t* bottom =
                    frame->pixels_argb + (size_t)(frame->height - 1 - y) * frame->width;
                for( int x = 0; x < frame->width; x++ )
                {
                    uint32_t tmp = top[x];
                    top[x] = bottom[x];
                    bottom[x] = tmp;
                }
            }
            if( frame->crop_height > frame->height )
                frame->crop_y = frame->crop_height - frame->height - frame->crop_y;
        }
    }
}

struct ToriRS_Sprite*
ToriRS_SpriteFromDat2Archive(
    struct RSCache_Dat2DiskArchive* archive,
    int sprite_id,
    const struct RSCache* profile)
{
    struct RSCache_Dat2SpritePack* pack;
    struct ToriRS_Sprite* sprite;

    if( !archive || sprite_id < 0 )
    {
        if( archive )
            RSCache_Dat2DiskArchiveFree(archive);
        return NULL;
    }

    pack = RSCache_Dat2SpritePackNewDecode(
        (const unsigned char*)archive->data,
        archive->data_size,
        RSCACHE_SPRITELOAD_FLAG_NORMALIZE | RSCache_Dat2SpriteFlags(profile));
    RSCache_Dat2DiskArchiveFree(archive);
    if( !pack || pack->count <= 0 )
    {
        if( pack )
            RSCache_Dat2SpritePackFree(pack);
        return NULL;
    }

    sprite = ToriRS_SpriteFromDat2Pack(pack, sprite_id);
    RSCache_Dat2SpritePackFree(pack);
    return sprite;
}
