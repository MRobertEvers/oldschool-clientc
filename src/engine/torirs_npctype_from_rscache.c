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

static void
torirs_npctype_copy_pairs_int(
    int** dst_from,
    int** dst_to,
    int* dst_count,
    const int* src_from,
    const int* src_to,
    int count)
{
    *dst_count = 0;
    if( count <= 0 || !src_from || !src_to )
        return;

    *dst_from = malloc((size_t)count * sizeof(int));
    *dst_to = malloc((size_t)count * sizeof(int));
    assert(*dst_from);
    assert(*dst_to);
    memcpy(*dst_from, src_from, (size_t)count * sizeof(int));
    memcpy(*dst_to, src_to, (size_t)count * sizeof(int));
    *dst_count = count;
}

/* dat2 decodes recolor/retexture pairs as signed shorts; widen to the
 * non-negative 16-bit values the loc/obj conversions already store. */
static void
torirs_npctype_copy_pairs_short(
    int** dst_from,
    int** dst_to,
    int* dst_count,
    const short* src_from,
    const short* src_to,
    int count)
{
    *dst_count = 0;
    if( count <= 0 || !src_from || !src_to )
        return;

    *dst_from = malloc((size_t)count * sizeof(int));
    *dst_to = malloc((size_t)count * sizeof(int));
    assert(*dst_from);
    assert(*dst_to);
    for( int i = 0; i < count; i++ )
    {
        (*dst_from)[i] = (int)(unsigned short)src_from[i];
        (*dst_to)[i] = (int)(unsigned short)src_to[i];
    }
    *dst_count = count;
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

    /* dat1-era npcs carry no retexture pairs. */
    torirs_npctype_copy_pairs_int(
        &npctype->recolors_from,
        &npctype->recolors_to,
        &npctype->recolor_count,
        src->recol_s,
        src->recol_d,
        src->recol_count);

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

    torirs_npctype_copy_pairs_short(
        &npctype->recolors_from,
        &npctype->recolors_to,
        &npctype->recolor_count,
        src->recolor_to_find,
        src->recolor_to_replace,
        src->recolor_count);
    torirs_npctype_copy_pairs_short(
        &npctype->retextures_from,
        &npctype->retextures_to,
        &npctype->retexture_count,
        src->retexture_to_find,
        src->retexture_to_replace,
        src->retexture_count);

    return npctype;
}
