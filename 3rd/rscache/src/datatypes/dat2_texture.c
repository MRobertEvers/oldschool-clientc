#include "dat2_texture.h"

#include "../rsbuffer.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static struct RSCache_Dat2Texture*
texture_decode_simplified(
    struct RSCache_Dat2Texture* def,
    char* data,
    int length)
{
    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)length);

    int sprite_id = g2(&buffer);
    def->average_hsl = g2(&buffer);
    def->opaque = g1(&buffer) == 1;
    def->animation_direction = g1(&buffer);
    def->animation_speed = g1(&buffer);

    def->sprite_ids_count = 1;
    def->sprite_ids = malloc(sizeof(int));
    assert(def->sprite_ids);
    def->sprite_ids[0] = sprite_id;
    def->sprite_types = NULL;
    def->transforms = NULL;

    return def;
}

uint32_t
RSCache_Dat2TextureEncodeProfile(
    const struct RSCache* cache,
    const struct RSCache_Dat2Texture* texture,
    uint8_t* out,
    uint32_t out_capacity)
{
    if( !texture || !out )
        return 0;

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    if( RSCache_Dat2TextureCodecVersion(cache) == RSCACHE_CODEC_TEXTURE_V2 )
    {
        /* Simplified record: a fixed seven bytes, one sprite. */
        p2(&buffer, texture->sprite_ids_count > 0 ? texture->sprite_ids[0] : 0);
        p2(&buffer, texture->average_hsl);
        p1(&buffer, texture->opaque ? 1 : 0);
        p1(&buffer, texture->animation_direction);
        p1(&buffer, texture->animation_speed);
        return buffer.position;
    }

    /* Full record. Unlike the config types this is a fixed layout, not an opcode
     * stream, so every field is always written. */
    int count = texture->sprite_ids_count;

    p2(&buffer, texture->average_hsl);
    p1(&buffer, texture->opaque ? 1 : 0);
    p1(&buffer, count);

    for( int i = 0; i < count; i++ )
        p2(&buffer, texture->sprite_ids[i]);

    if( count > 1 )
    {
        for( int i = 0; i < count - 1; i++ )
            p1(&buffer, texture->sprite_types ? texture->sprite_types[i] : 0);

        /* A second run of count-1 bytes the decoder reads and discards. Their
         * original values are not recoverable, so they re-encode as zero — this is
         * the one field that keeps a multi-sprite texture from being byte-exact. */
        for( int i = 0; i < count - 1; i++ )
            p1(&buffer, 0);
    }

    for( int i = 0; i < count; i++ )
        p4(&buffer, texture->transforms ? texture->transforms[i] : 0);

    p1(&buffer, texture->animation_direction);
    p1(&buffer, texture->animation_speed);

    return buffer.position;
}

int
RSCache_Dat2TextureCodecVersion(const struct RSCache* cache)
{
    /* Full record by default: the simplified 7-byte form is the modern one, and
     * a cache with no identifying information is more likely to be old. A modern
     * cache always carries a timestamp archive revision, which the threshold
     * catches. */
    int derived = RSCache_RevisionAtLeastOsrs(
                      cache,
                      RSCACHE_TYPE_TEXTURE,
                      233,
                      RSCACHE_TEXTURE_ARCHIVE_REV_SIMPLIFIED,
                      false)
                      ? RSCACHE_CODEC_TEXTURE_V2
                      : RSCACHE_CODEC_TEXTURE_V1;
    return RSCache_CodecVersionOr(cache, RSCACHE_TYPE_TEXTURE, derived);
}

struct RSCache_Dat2Texture*
RSCache_Dat2TextureNewDecodeProfile(
    const struct RSCache* cache,
    char* data,
    int length)
{
    struct RSCache_Dat2Texture* def = malloc(sizeof(struct RSCache_Dat2Texture));
    assert(def);
    memset(def, 0, sizeof(struct RSCache_Dat2Texture));

    if( RSCache_Dat2TextureCodecVersion(cache) == RSCACHE_CODEC_TEXTURE_V2 )
        return texture_decode_simplified(def, data, length);

    return RSCache_Dat2TextureDecodeInplace(def, data, length);
}

void
RSCache_Dat2TextureFree(struct RSCache_Dat2Texture* texture)
{
    if( !texture )
        return;
    RSCache_Dat2TextureFreeInplace(texture);
    free(texture);
}

void
RSCache_Dat2TextureFreeInplace(struct RSCache_Dat2Texture* texture)
{
    if( !texture )
        return;

    free(texture->sprite_ids);
    free(texture->sprite_types);
    free(texture->transforms);
}

struct RSCache_Dat2Texture*
RSCache_Dat2TextureDecodeInplace(
    struct RSCache_Dat2Texture* def,
    char* data,
    int length)
{
    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)length);

    def->average_hsl = g2(&buffer);
    def->opaque = g1b(&buffer) != 0;

    int count = g1(&buffer);
    def->sprite_ids_count = count;

    def->sprite_ids = malloc(count * sizeof(int));
    assert(def->sprite_ids);

    for( int i = 0; i < count; i++ )
        def->sprite_ids[i] = g2(&buffer);

    if( count > 1 )
    {
        def->sprite_types = malloc((count - 1) * sizeof(int));
        assert(def->sprite_types);

        for( int i = 0; i < count - 1; i++ )
            def->sprite_types[i] = g1(&buffer);
    }

    if( count > 1 )
    {
        // unused?
        for( int i = 0; i < count - 1; i++ )
            g1(&buffer);
    }

    def->transforms = malloc(count * sizeof(int));
    assert(def->transforms);

    for( int i = 0; i < count; i++ )
        def->transforms[i] = g4(&buffer);

    def->animation_direction = g1(&buffer);
    def->animation_speed = g1(&buffer);

    return def;
}
