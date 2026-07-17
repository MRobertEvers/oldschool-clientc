#include "engine/dat1/dat1_tasks.h"

#include <assert.h>

struct ToriRS_Task*
CreateTask_Dat1ComponentLoad(
    struct CacheProvider* provider,
    int packed_id)
{
    int iface_id;

    assert(provider);
    iface_id = packed_id >> 16;
    return CreateTask_Dat1ComponentPackLoad(provider, iface_id);
}

/*
 * Dat1 sprites/fonts are name-keyed (media/title jagfiles), not numeric archive ids.
 * Numeric CreateTask_SpriteLoad/FontLoad is a dat2 API. Dat1 named loads are done by
 * uitree_builder via jagfile archive tasks + converters.
 */
struct ToriRS_Task*
CreateTask_Dat1SpriteLoad(
    struct CacheProvider* provider,
    int sprite_id)
{
    (void)provider;
    (void)sprite_id;
    return NULL;
}

struct ToriRS_Task*
CreateTask_Dat1FontLoad(
    struct CacheProvider* provider,
    int font_id)
{
    (void)provider;
    (void)font_id;
    return NULL;
}
