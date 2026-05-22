#include "src/osrs/rscache/tables/sprites.h"

/**
 * Stub for src2 builds that use texture.c but not the modern sprite-pack loader.
 * texture_new_from_definition is unused; only texture_new_from_texture_sprite is called.
 */
void
sprite_pack_free(struct CacheSpritePack* pack)
{
    (void)pack;
}
