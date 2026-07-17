#include "dat2_texture.h"

#include "../rsbuffer.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct RSCache_Dat2Texture*
RSCache_Dat2TextureNewDecode(
    char* data,
    int length)
{
    struct RSCache_Dat2Texture* def = malloc(sizeof(struct RSCache_Dat2Texture));
    assert(def);
    memset(def, 0, sizeof(struct RSCache_Dat2Texture));

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
