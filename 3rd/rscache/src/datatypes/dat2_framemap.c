#include "dat2_framemap.h"

#include "../dat2disk.h"
#include "../rsbuffer.h"
#include "../rscache_profile.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
RSCache_Dat2FramemapCodecVersion(const struct RSCache* cache)
{
    assert(cache);
    /* RS2 game rev 530 introduced per-transform masks (and already had
     * transform_actor from 481). OSRS never took either change. */
    int derived = RSCACHE_CODEC_FRAMEMAP_V1;
    if( RSCache_RevisionAtLeastRs2(
            cache, RSCACHE_TYPE_FRAMEMAP, 530, RSCACHE_GROUP_REVISION_UNKNOWN, false) )
        derived = RSCACHE_CODEC_FRAMEMAP_V3;
    else if( RSCache_RevisionAtLeastRs2(
                 cache, RSCACHE_TYPE_FRAMEMAP, 481, RSCACHE_GROUP_REVISION_UNKNOWN, false) )
        derived = RSCACHE_CODEC_FRAMEMAP_V2;
    return RSCache_CodecVersionOr(cache, RSCACHE_TYPE_FRAMEMAP, derived);
}

struct RSCache_Dat2Framemap*
RSCache_Dat2FramemapNewFromCache(
    struct RSCache_Dat2Disk* cache,
    int framemap_id)
{
    struct RSCache_Dat2DiskArchive* archive = RSCache_Dat2DiskArchiveNewLoad(
        cache, RSCache_Dat2DiskTableId(cache, RSCACHE_DAT2_TABLE_SKELETONS), framemap_id);
    if( !archive )
    {
        printf("Failed to load framemap %d\n", framemap_id);
        return NULL;
    }

    struct RSCache_Dat2Framemap* framemap = RSCache_Dat2FramemapNewDecodeProfile(
        RSCache_Dat2DiskProfile(cache), framemap_id, archive->data, archive->data_size);

    RSCache_Dat2DiskArchiveFree(archive);

    return framemap;
}

struct RSCache_Dat2Framemap*
RSCache_Dat2FramemapNewFromArchive(
    struct RSCache_Dat2DiskArchive* archive,
    int framemap_id)
{
    if( !archive )
        return NULL;

    return RSCache_Dat2FramemapNewDecode2(framemap_id, archive->data, archive->data_size);
}

struct RSCache_Dat2Framemap*
RSCache_Dat2FramemapNewFromArchiveProfile(
    const struct RSCache* cache,
    struct RSCache_Dat2DiskArchive* archive,
    int framemap_id)
{
    if( !archive )
        return NULL;

    return RSCache_Dat2FramemapNewDecodeProfile(
        cache, framemap_id, archive->data, archive->data_size);
}

int
RSCache_Dat2FramemapIdFromFrameArchive(
    char* data,
    int data_size)
{
    (void)data_size;
    int framemap_id = (data[0] & 0xFF) << 8 | (data[1] & 0xFF);
    return framemap_id;
}

static struct RSCache_Dat2Framemap*
framemap_new_decode(
    int id,
    struct RSCache_Buffer* buffer,
    int codec_version);

struct RSCache_Dat2Framemap*
RSCache_Dat2FramemapNewDecodeCodec(
    int id,
    char* data,
    int data_size,
    int codec_version)
{
    struct RSCache_Buffer buffer = {
        .data = (uint8_t*)(data), .size = (uint32_t)(data_size), .position = 0
    };
    return framemap_new_decode(id, &buffer, codec_version);
}

struct RSCache_Dat2Framemap*
RSCache_Dat2FramemapNewDecodeProfile(
    const struct RSCache* cache,
    int id,
    char* data,
    int data_size)
{
    return RSCache_Dat2FramemapNewDecodeCodec(
        id, data, data_size, RSCache_Dat2FramemapCodecVersion(cache));
}

struct RSCache_Dat2Framemap*
RSCache_Dat2FramemapNewDecode2(
    int id,
    char* data,
    int data_size)
{
    return RSCache_Dat2FramemapNewDecodeCodec(id, data, data_size, RSCACHE_CODEC_FRAMEMAP_V1);
}

static struct RSCache_Dat2Framemap*
framemap_new_decode(
    int id,
    struct RSCache_Buffer* buffer,
    int codec_version)
{
    assert(
        codec_version == RSCACHE_CODEC_FRAMEMAP_V1 || codec_version == RSCACHE_CODEC_FRAMEMAP_V2 ||
        codec_version == RSCACHE_CODEC_FRAMEMAP_V3);

    struct RSCache_Dat2Framemap* def = malloc(sizeof(struct RSCache_Dat2Framemap));
    memset(def, 0, sizeof(struct RSCache_Dat2Framemap));

    def->id = id;
    def->length = g1(buffer);
    def->types = malloc((size_t)def->length * sizeof(int));
    def->bone_groups = malloc((size_t)def->length * sizeof(int*));
    def->bone_groups_lengths = malloc((size_t)def->length * sizeof(int));
    memset(def->types, 0, (size_t)def->length * sizeof(int));
    memset(def->bone_groups, 0, (size_t)def->length * sizeof(int*));
    memset(def->bone_groups_lengths, 0, (size_t)def->length * sizeof(int));

    for( int i = 0; i < def->length; i++ )
        def->types[i] = g1(buffer);

    /* V2+: transform_actor byte per transform (RS >= 481). */
    if( codec_version >= RSCACHE_CODEC_FRAMEMAP_V2 )
    {
        def->has_transform_actor = true;
        def->transform_actor = malloc((size_t)def->length);
        for( int i = 0; i < def->length; i++ )
            def->transform_actor[i] = (uint8_t)g1(buffer);
    }

    /* V3: masks u16 per transform (RS >= 530). */
    if( codec_version >= RSCACHE_CODEC_FRAMEMAP_V3 )
    {
        def->has_masks = true;
        def->masks = malloc((size_t)def->length * sizeof(uint16_t));
        for( int i = 0; i < def->length; i++ )
            def->masks[i] = (uint16_t)g2(buffer);
    }

    for( int i = 0; i < def->length; i++ )
    {
        int group_length = g1(buffer);
        def->bone_groups_lengths[i] = group_length;
        def->bone_groups[i] = malloc((size_t)group_length * sizeof(int));
        memset(def->bone_groups[i], 0, (size_t)group_length * sizeof(int));
    }

    for( int i = 0; i < def->length; i++ )
    {
        for( int j = 0; j < def->bone_groups_lengths[i]; j++ )
            def->bone_groups[i][j] = g1(buffer);
    }

    /* Trailing skeletal-base blob / modern unread pad — keep verbatim. */
    if( buffer->position < buffer->size )
    {
        def->tail_size = (int)(buffer->size - buffer->position);
        def->tail = malloc((size_t)def->tail_size);
        if( def->tail )
            memcpy(def->tail, buffer->data + buffer->position, (size_t)def->tail_size);
        buffer->position = buffer->size;
    }

    return def;
}

uint32_t
RSCache_Dat2FramemapEncodeBound(const struct RSCache_Dat2Framemap* def)
{
    if( !def )
        return 0;

    /* 1 length byte, then a type byte and a group-length byte per entry, then one
     * byte per bone in every group. Plus optional actor / masks / tail. */
    uint32_t total = 1u + (uint32_t)def->length * 2u;
    for( int i = 0; i < def->length; i++ )
        total += (uint32_t)def->bone_groups_lengths[i];
    if( def->has_transform_actor )
        total += (uint32_t)def->length;
    if( def->has_masks )
        total += (uint32_t)def->length * 2u;
    if( def->tail_size > 0 )
        total += (uint32_t)def->tail_size;
    return total;
}

uint32_t
RSCache_Dat2FramemapEncode(
    const struct RSCache_Dat2Framemap* def,
    uint8_t* out,
    uint32_t out_capacity)
{
    if( !def || !out )
        return 0;
    if( def->has_transform_actor && !def->transform_actor )
        return 0;
    if( def->has_masks && !def->masks )
        return 0;
    if( def->tail_size > 0 && !def->tail )
        return 0;
    if( out_capacity < RSCache_Dat2FramemapEncodeBound(def) )
        return 0;

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    /* Fixed layout, not an opcode stream. Pass order matches Dat2SeqBase.load:
     * types, optional transform_actor, optional masks, group lengths, group
     * contents, then any trailing skeletal-base / pad bytes. */
    p1(&buffer, def->length);

    for( int i = 0; i < def->length; i++ )
        p1(&buffer, def->types[i]);

    if( def->has_transform_actor )
    {
        for( int i = 0; i < def->length; i++ )
            p1(&buffer, def->transform_actor[i]);
    }

    if( def->has_masks )
    {
        for( int i = 0; i < def->length; i++ )
            p2(&buffer, def->masks[i]);
    }

    for( int i = 0; i < def->length; i++ )
        p1(&buffer, def->bone_groups_lengths[i]);

    for( int i = 0; i < def->length; i++ )
    {
        for( int j = 0; j < def->bone_groups_lengths[i]; j++ )
            p1(&buffer, def->bone_groups[i][j]);
    }

    if( def->tail_size > 0 )
    {
        memcpy(buffer.data + buffer.position, def->tail, (size_t)def->tail_size);
        buffer.position += (uint32_t)def->tail_size;
    }

    return buffer.position;
}

void
RSCache_Dat2FramemapFree(struct RSCache_Dat2Framemap* def)
{
    if( !def )
        return;

    if( def->types )
        free(def->types);

    if( def->bone_groups )
    {
        for( int i = 0; i < def->length; i++ )
        {
            if( def->bone_groups[i] )
                free(def->bone_groups[i]);
        }
        free(def->bone_groups);
    }

    free(def->bone_groups_lengths);
    free(def->transform_actor);
    free(def->masks);
    free(def->tail);
    free(def);
}
