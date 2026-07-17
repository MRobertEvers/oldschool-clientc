#include "engine/torirs_npctype_from_rscache.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void
torirs_copy_menu_actions(
    char actions[TORIRS_MENU_ACTION_SLOTS][TORIRS_MENU_ACTION_LEN],
    char* const* src_actions)
{
    for( int i = 0; i < TORIRS_MENU_ACTION_SLOTS; i++ )
        actions[i][0] = '\0';

    if( !src_actions )
        return;

    for( int i = 0; i < TORIRS_MENU_ACTION_SLOTS; i++ )
    {
        if( src_actions[i] && src_actions[i][0] != '\0' )
        {
            strncpy(actions[i], src_actions[i], TORIRS_MENU_ACTION_LEN - 1);
            actions[i][TORIRS_MENU_ACTION_LEN - 1] = '\0';
        }
    }
}

static void
torirs_npctype_copy_models(
    struct ToriRS_Npctype* dst,
    const int* models,
    int models_count)
{
    dst->models_count = models_count;
    if( models_count <= 0 )
        return;

    dst->models = malloc((size_t)models_count * sizeof(int));
    assert(dst->models);
    memcpy(dst->models, models, (size_t)models_count * sizeof(int));
}

struct ToriRS_Npctype*
ToriRS_NpctypeFromRSCacheDat1(
    int npc_id,
    const struct RSCache_Dat1ConfigNpc* src)
{
    struct ToriRS_Npctype* npctype;

    assert(src);

    npctype = calloc(1, sizeof(*npctype));
    assert(npctype);

    npctype->id = npc_id;
    if( src->name )
    {
        strncpy(npctype->name, src->name, TORIRS_NAME_MAX - 1);
        npctype->name[TORIRS_NAME_MAX - 1] = '\0';
    }

    torirs_copy_menu_actions(npctype->actions, src->op);
    npctype->combat_level = src->vislevel;
    npctype->size = src->size;
    torirs_npctype_copy_models(npctype, src->models, src->models_count);

    return npctype;
}

struct ToriRS_Npctype*
ToriRS_NpctypeFromRSCacheDat2(
    int npc_id,
    const struct RSCache_Dat2ConfigNpc* src)
{
    struct ToriRS_Npctype* npctype;

    assert(src);

    npctype = calloc(1, sizeof(*npctype));
    assert(npctype);

    npctype->id = npc_id;
    if( src->name )
    {
        strncpy(npctype->name, src->name, TORIRS_NAME_MAX - 1);
        npctype->name[TORIRS_NAME_MAX - 1] = '\0';
    }

    torirs_copy_menu_actions(npctype->actions, src->actions);
    npctype->combat_level = src->combat_level;
    npctype->size = src->size;
    torirs_npctype_copy_models(npctype, src->models, src->models_count);

    return npctype;
}
