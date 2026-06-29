#include "appearance.h"

#include "../buildcache/dat1_buildcache.h"
#include "osrs/datatypes/appearances.h"
#include "osrs/rscache/dat1a/dat1a_config_idk.h"
#include "osrs/rscache/dat1a/dat1a_config_obj.h"

#include <assert.h>
#include <stdbool.h>

/* 0x100 = IDK slot, 0x200 = equipped obj model (src/server/server.c). */
const int RUNESCAPE_EXAMPLE_PLAYER_APPEARANCE[RUNESCAPE_APPEARANCE_SLOT_COUNT] = {
    0,          0, 0,          0x200 + 1333, /* Rune scimitar */
    0x100 + 18, 0, 0x100 + 26, 0x100 + 36,   0x100 + 0, 0x100 + 33, 0x100 + 42, 0x100 + 10,
};

static bool
int_list_add_unique(
    int* model_ids,
    int* count,
    int capacity,
    int model_id)
{
    int i;

    if( model_id < 0 )
        return false;

    for( i = 0; i < *count; i++ )
    {
        if( model_ids[i] == model_id )
            return true;
    }

    if( *count >= capacity )
        return false;

    model_ids[(*count)++] = model_id;
    return true;
}

static void
append_idk_model_ids(
    struct Dat1BuildCache* dat1_bc,
    int idk_id,
    int* model_ids,
    int* count,
    int capacity)
{
    struct RSCacheDat1A_ConfigIdk* idk = dat1_buildcache_idk_get(dat1_bc, idk_id);
    int i;

    if( !idk || !idk->models )
        return;

    for( i = 0; i < idk->models_count; i++ )
        int_list_add_unique(model_ids, count, capacity, idk->models[i]);
}

static void
append_obj_manwear_model_ids(
    struct Dat1BuildCache* dat1_bc,
    int obj_id,
    int* model_ids,
    int* count,
    int capacity)
{
    struct RSCacheDat1A_ConfigObj* obj = dat1_buildcache_obj_get(dat1_bc, obj_id);

    if( !obj )
        return;

    int_list_add_unique(model_ids, count, capacity, obj->manwear);
    int_list_add_unique(model_ids, count, capacity, obj->manwear2);
    int_list_add_unique(model_ids, count, capacity, obj->manwear3);
}

int
runescape_appearance_collect_model_ids(
    struct Dat1BuildCache* dat1_bc,
    const int appearance[RUNESCAPE_APPEARANCE_SLOT_COUNT],
    int* model_ids,
    int capacity)
{
    uint16_t slots[RUNESCAPE_APPEARANCE_SLOT_COUNT];
    int count = 0;
    int slot;

    assert(dat1_bc && appearance && model_ids && capacity > 0);

    for( slot = 0; slot < RUNESCAPE_APPEARANCE_SLOT_COUNT; slot++ )
        slots[slot] = (uint16_t)appearance[slot];

    for( slot = 0; slot < RUNESCAPE_APPEARANCE_SLOT_COUNT; slot++ )
    {
        struct AppearanceOp op;

        appearances_decode(&op, slots, slot);
        if( op.kind == APPEARANCE_KIND_IDK )
            append_idk_model_ids(dat1_bc, (int)op.id, model_ids, &count, capacity);
        else if( op.kind == APPEARANCE_KIND_OBJ )
            append_obj_manwear_model_ids(dat1_bc, (int)op.id, model_ids, &count, capacity);
    }

    return count;
}
