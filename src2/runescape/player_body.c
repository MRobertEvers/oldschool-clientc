#include "player_body.h"

#include "../buildcache/dat1_buildcache.h"
#include "../buildcache/dat2_buildcache.h"
#include "../games/runescape.h"
#include "../toriauxlib/cache/toriauxlibcache.h"
#include "../toriauxlib/cache/toriauxlibcache_submit.h"
#include "../toriauxlib/core/toriauxlibcore.h"
#include "../toriauxlib/td/toriauxlibtd.h"
#include "osrs/datatypes/appearances.h"
#include "osrs/rscache/dat1a/dat1a_config_npc.h"
#include "osrs/rscache/dat2a/dat2a_config_npctype.h"
#include "toridraw/toridraw_light_model.h"
#include "toridraw/toridraw_model.h"
#include "toridraw/toridraw_model_transform.h"

#include <assert.h>

static struct ToriDraw_ModelHandle
scene_model_from_cache(
    struct GameRunescape* game,
    int model_id)
{
    struct ToriDraw_ModelHandle cached;
    struct ToriDraw_Model* model;
    struct ToriDraw_ModelHandle hnd;

    cached = ToriAuxLibTD_Model(game->td, model_id);
    if( !ToriDraw_ModelGetBoundsCylinder(cached) )
        return (struct ToriDraw_ModelHandle){ .kind = TORIDRAWMK_NONE };

    model = ToriDraw_ModelCopy(cached.u.model.model);
    if( !model )
        return (struct ToriDraw_ModelHandle){ .kind = TORIDRAWMK_NONE };

    hnd = (struct ToriDraw_ModelHandle){
        .kind = TORIDRAWMK_MODEL,
        .u.model.model = model,
    };

    if( ToriDraw_ModelIsLightable(model) )
    {
        ToriDraw_LightModelDefault(hnd, 0, 0);
        ToriDraw_ModelFreeNormals(model);
    }

    return hnd;
}

static struct ToriDraw_Model*
scene_model_copy_unlit(
    struct GameRunescape* game,
    int model_id)
{
    struct ToriDraw_ModelHandle cached;
    struct ToriDraw_Model* model;

    cached = ToriAuxLibTD_Model(game->td, model_id);
    if( !ToriDraw_ModelGetBoundsCylinder(cached) )
        return NULL;

    return ToriDraw_ModelCopy(cached.u.model.model);
}

struct WorldEntityFacet_IdleAnimations
runescape_npc_animation_from_config(
    struct GameRunescape* game,
    int npc_id)
{
    struct ToriAuxLibCache* c;
    struct WorldEntityFacet_IdleAnimations animation = {
        .readyanim = -1,
        .walkanim = -1,
        .turnanim = -1,
        .runanim = -1,
        .walkanim_b = -1,
        .walkanim_r = -1,
        .walkanim_l = -1,
    };

    assert(game);
    assert(game->td);

    c = ToriAuxLibTD_C(game->td);
    assert(c);

    if( ToriAuxLibCache_Mode(c) == TORIAUXLIBCACHE_MODE_DAT1 )
    {
        struct RSCacheDat1A_ConfigNpc* npc = dat1_buildcache_npc_get(dat1(c), npc_id);
        if( !npc )
            return animation;

        animation.readyanim = npc->readyanim;
        animation.walkanim = npc->walkanim;
        animation.walkanim_b = npc->walkanim_b;
        animation.walkanim_r = npc->walkanim_r;
        animation.walkanim_l = npc->walkanim_l;
    }
    else
    {
        struct RSCacheDat2A_ConfigNpctype* npc = dat2_buildcache_npctype_get(dat2(c), npc_id);
        if( !npc )
            return animation;

        animation.readyanim = npc->standing_animation;
        animation.walkanim = npc->walking_animation;
        animation.turnanim = npc->rotate180_animation;
        animation.runanim = npc->run_animation;
        animation.walkanim_l = npc->rotate_left_animation;
        animation.walkanim_r = npc->rotate_right_animation;
    }

    return animation;
}

int
runescape_npc_size_from_config(
    struct GameRunescape* game,
    int npc_id)
{
    struct ToriAuxLibCache* c;

    assert(game);
    assert(game->td);

    c = ToriAuxLibTD_C(game->td);
    assert(c);

    if( ToriAuxLibCache_Mode(c) == TORIAUXLIBCACHE_MODE_DAT1 )
    {
        struct RSCacheDat1A_ConfigNpc* npc = dat1_buildcache_npc_get(dat1(c), npc_id);
        if( !npc )
            return 1;
        return npc->size > 0 ? npc->size : 1;
    }

    {
        struct RSCacheDat2A_ConfigNpctype* npc = dat2_buildcache_npctype_get(dat2(c), npc_id);
        if( !npc )
            return 1;
        return npc->size > 0 ? npc->size : 1;
    }
}

struct ToriDraw_ModelHandle
runescape_npc_body_build(
    struct GameRunescape* game,
    int npc_id)
{
    struct ToriAuxLibCache* c;
    struct ToriDraw_Model* pieces[64];
    int piece_count = 0;
    struct ToriDraw_Model* body;
    struct ToriDraw_ModelHandle hnd;
    int i;

    assert(game);
    assert(game->td);

    c = ToriAuxLibTD_C(game->td);
    assert(c);

    if( ToriAuxLibCache_Mode(c) == TORIAUXLIBCACHE_MODE_DAT1 )
    {
        struct RSCacheDat1A_ConfigNpc* npc = dat1_buildcache_npc_get(dat1(c), npc_id);
        if( !npc )
            return (struct ToriDraw_ModelHandle){ .kind = TORIDRAWMK_NONE };
        if( npc->models_count <= 0 )
            return (struct ToriDraw_ModelHandle){ .kind = TORIDRAWMK_NONE };

        for( i = 0; i < npc->models_count && piece_count < 64; i++ )
        {
            struct ToriDraw_Model* piece = scene_model_copy_unlit(game, npc->models[i]);
            if( piece )
                pieces[piece_count++] = piece;
        }

        if( piece_count == 0 )
            return (struct ToriDraw_ModelHandle){ .kind = TORIDRAWMK_NONE };

        body = ToriDraw_ModelNewMerge(pieces, piece_count);
        for( i = 0; i < piece_count; i++ )
            ToriDraw_ModelFree(pieces[i]);

        if( !body )
            return (struct ToriDraw_ModelHandle){ .kind = TORIDRAWMK_NONE };

        for( i = 0; i < npc->recol_count; i++ )
            ToriDraw_ModelRecolor(body, npc->recol_s[i], npc->recol_d[i]);

        if( npc->resizeh != 128 || npc->resizev != 128 )
            ToriDraw_ModelScale(body, npc->resizeh, npc->resizeh, npc->resizev);
    }
    else
    {
        struct RSCacheDat2A_ConfigNpctype* npc = dat2_buildcache_npctype_get(dat2(c), npc_id);
        int width_scale;
        int height_scale;

        if( !npc )
            return (struct ToriDraw_ModelHandle){ .kind = TORIDRAWMK_NONE };
        if( npc->models_count <= 0 )
            return (struct ToriDraw_ModelHandle){ .kind = TORIDRAWMK_NONE };

        for( i = 0; i < npc->models_count && piece_count < 64; i++ )
        {
            struct ToriDraw_Model* piece = scene_model_copy_unlit(game, npc->models[i]);
            if( piece )
                pieces[piece_count++] = piece;
        }

        if( piece_count == 0 )
            return (struct ToriDraw_ModelHandle){ .kind = TORIDRAWMK_NONE };

        body = ToriDraw_ModelNewMerge(pieces, piece_count);
        for( i = 0; i < piece_count; i++ )
            ToriDraw_ModelFree(pieces[i]);

        if( !body )
            return (struct ToriDraw_ModelHandle){ .kind = TORIDRAWMK_NONE };

        for( i = 0; i < npc->recolor_count; i++ )
            ToriDraw_ModelRecolor(body, npc->recolor_to_find[i], npc->recolor_to_replace[i]);

        for( i = 0; i < npc->retexture_count; i++ )
            ToriDraw_ModelRetexture(body, npc->retexture_to_find[i], npc->retexture_to_replace[i]);

        width_scale = npc->width_scale > 0 ? npc->width_scale : 128;
        height_scale = npc->height_scale > 0 ? npc->height_scale : 128;
        if( width_scale != 128 || height_scale != 128 )
            ToriDraw_ModelScale(body, width_scale, width_scale, height_scale);
    }

    ToriDraw_ModelSetBoundsCylinder(body);
    hnd = (struct ToriDraw_ModelHandle){
        .kind = TORIDRAWMK_MODEL,
        .u.model.model = body,
    };
    if( ToriDraw_ModelIsLightable(body) )
    {
        ToriDraw_LightModelDefault(hnd, 0, 0);
        ToriDraw_ModelFreeNormals(body);
    }
    return hnd;
}

struct ToriDraw_ModelHandle
runescape_player_body_build(
    struct GameRunescape* game,
    const int appearance[RUNESCAPE_APPEARANCE_SLOT_COUNT])
{
    struct ToriAuxLibCache* c;
    struct ToriAuxLibCore* core;
    struct ToriDraw_Model* pieces[RUNESCAPE_APPEARANCE_SLOT_COUNT];
    int piece_count = 0;
    struct ToriDraw_Model* body;
    struct ToriDraw_ModelHandle hnd;
    uint16_t slots[RUNESCAPE_APPEARANCE_SLOT_COUNT];
    int slot;

    assert(game);
    assert(game->td);
    assert(appearance);

    c = ToriAuxLibTD_C(game->td);
    core = ToriAuxLibTD_Core(game->td);
    assert(c);
    assert(core);

    for( slot = 0; slot < RUNESCAPE_APPEARANCE_SLOT_COUNT; slot++ )
        slots[slot] = (uint16_t)appearance[slot];

    for( slot = 0; slot < RUNESCAPE_APPEARANCE_SLOT_COUNT; slot++ )
    {
        struct AppearanceOp op;
        struct ToriAuxLibCore_Model* core_piece = NULL;
        struct ToriDraw_Model* td_piece;

        appearances_decode(&op, slots, slot);
        if( op.kind == APPEARANCE_KIND_IDK )
        {
            if( ToriAuxLibCache_Mode(c) == TORIAUXLIBCACHE_MODE_DAT1 )
            {
                if( !ToriAuxLibCore_IdkModelHas(core, (int)op.id) )
                    ToriAuxLibCache_SubmitIdkModelFromDat1(c, (int)op.id);
            }
            else
            {
                if( !ToriAuxLibCore_IdkModelHas(core, (int)op.id) )
                    ToriAuxLibCache_SubmitIdkModelFromDat2(c, (int)op.id);
            }
            core_piece = ToriAuxLibCore_IdkModelGet(core, (int)op.id);
        }
        else if( op.kind == APPEARANCE_KIND_OBJ )
        {
            if( ToriAuxLibCache_Mode(c) == TORIAUXLIBCACHE_MODE_DAT1 )
            {
                if( !ToriAuxLibCore_ObjModelHas(core, (int)op.id) )
                    ToriAuxLibCache_SubmitObjModelFromDat1(c, (int)op.id);
            }
            else
            {
                if( !ToriAuxLibCore_ObjModelHas(core, (int)op.id) )
                    ToriAuxLibCache_SubmitObjModelFromDat2(c, (int)op.id);
            }
            core_piece = ToriAuxLibCore_ObjModelGet(core, (int)op.id);
        }

        if( !core_piece )
            continue;

        td_piece = ToriAuxLibTD_ModelNewFromCore(core_piece);
        if( td_piece )
            pieces[piece_count++] = td_piece;
    }

    if( piece_count == 0 )
        return scene_model_from_cache(game, RUNESCAPE_PLAYER_PLACEHOLDER_MODEL_ID);

    body = ToriDraw_ModelNewMerge(pieces, piece_count);
    for( slot = 0; slot < piece_count; slot++ )
        ToriDraw_ModelFree(pieces[slot]);

    if( !body )
        return (struct ToriDraw_ModelHandle){ .kind = TORIDRAWMK_NONE };

    ToriDraw_ModelSetBoundsCylinder(body);
    hnd = (struct ToriDraw_ModelHandle){
        .kind = TORIDRAWMK_MODEL,
        .u.model.model = body,
    };
    if( ToriDraw_ModelIsLightable(body) )
    {
        ToriDraw_LightModelDefault(hnd, 0, 0);
        ToriDraw_ModelFreeNormals(body);
    }
    return hnd;
}
