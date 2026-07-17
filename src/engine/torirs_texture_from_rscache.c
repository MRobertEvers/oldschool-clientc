#include "engine/torirs_texture_from_rscache.h"

#include <assert.h>
#include <stdlib.h>

struct ToriRS_Texture*
ToriRS_TextureFromRSCache(
    int texture_id,
    const struct RSCache_Dat2Texture* src)
{
    struct ToriRS_Texture* texture;

    assert(src);
    (void)texture_id;

    texture = calloc(1, sizeof(*texture));
    assert(texture);

    /* Pixel data (texels/width/height) is decoded from the referenced sprites
     * elsewhere; the config-derived texture only carries these metadata fields. */
    texture->texels = NULL;
    texture->width = 0;
    texture->height = 0;
    texture->opaque = src->opaque;
    texture->animation_direction = src->animation_direction;
    texture->animation_speed = src->animation_speed;
    texture->average_hsl = src->average_hsl;

    return texture;
}
