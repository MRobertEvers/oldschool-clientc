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
torirs_npctype_copy_heads(
    struct ToriRS_Npctype* dst,
    const int* heads,
    int heads_count)
{
    dst->heads_count = heads_count;
    if( heads_count <= 0 || !heads )
    {
        dst->heads_count = 0;
        return;
    }

    dst->heads = malloc((size_t)heads_count * sizeof(int));
    assert(dst->heads);
    memcpy(dst->heads, heads, (size_t)heads_count * sizeof(int));
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
    if( src->desc )
    {
        strncpy(npctype->desc, src->desc, TORIRS_DESC_MAX - 1);
        npctype->desc[TORIRS_DESC_MAX - 1] = '\0';
    }

    torirs_copy_menu_actions(npctype->actions, src->op);
    npctype->combat_level = src->vislevel;
    npctype->size = src->size;
    torirs_npctype_copy_models(npctype, src->models, src->models_count);
    torirs_npctype_copy_heads(npctype, src->heads, src->heads_count);

    /* dat1-era npcs carry no retexture pairs. */
    torirs_npctype_copy_pairs_int(
        &npctype->recolors_from,
        &npctype->recolors_to,
        &npctype->recolor_count,
        src->recol_s,
        src->recol_d,
        src->recol_count);

    npctype->readyanim = src->readyanim;
    npctype->walkanim = src->walkanim;
    npctype->walkanim_b = src->walkanim_b;
    npctype->walkanim_r = src->walkanim_r;
    npctype->walkanim_l = src->walkanim_l;
    npctype->turn_speed = src->turnspeed;
    npctype->width_scale = src->resizeh > 0 ? src->resizeh : 128;
    npctype->height_scale = src->resizev > 0 ? src->resizev : 128;
    npctype->alwaysontop = src->alwaysontop;

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
    /* No desc: dat2 (OSRS) NPC config carries no examine opcode — NPC examine is
     * server-driven there. npctype->desc stays "" (calloc) so the handler falls
     * back to "It's a <name>.". */

    torirs_copy_menu_actions(npctype->actions, src->actions);
    npctype->combat_level = src->combat_level;
    npctype->size = src->size;
    torirs_npctype_copy_models(npctype, src->models, src->models_count);
    torirs_npctype_copy_heads(npctype, src->chathead_models, src->chathead_models_count);

    torirs_npctype_copy_pairs_int(
        &npctype->recolors_from,
        &npctype->recolors_to,
        &npctype->recolor_count,
        src->recolor_to_find,
        src->recolor_to_replace,
        src->recolor_count);
    torirs_npctype_copy_pairs_int(
        &npctype->retextures_from,
        &npctype->retextures_to,
        &npctype->retexture_count,
        src->retexture_to_find,
        src->retexture_to_replace,
        src->retexture_count);

    npctype->readyanim = src->standing_animation > 0 ? src->standing_animation : -1;
    npctype->walkanim = src->walking_animation > 0 ? src->walking_animation : -1;
    npctype->walkanim_b = src->rotate180_animation > 0 ? src->rotate180_animation : -1;
    npctype->walkanim_r = src->rotate_right_animation > 0 ? src->rotate_right_animation : -1;
    npctype->walkanim_l = src->rotate_left_animation > 0 ? src->rotate_left_animation : -1;
    /* dat2 npc configs carry no turnspeed field; the reference default is 32. */
    npctype->turn_speed = 32;
    /* The dat2 decoder calloc's its struct, so an absent opcode 97/98 reads 0, not the
     * 128 the reference defaults to — map it here or every unscaled npc collapses. */
    npctype->width_scale = src->width_scale > 0 ? src->width_scale : 128;
    npctype->height_scale = src->height_scale > 0 ? src->height_scale : 128;
    npctype->alwaysontop = src->has_render_priority;

    return npctype;
}
