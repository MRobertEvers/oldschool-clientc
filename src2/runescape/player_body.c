#include "player_body.h"

#include "../games/runescape.h"
#include "../toriauxlib/c/toriauxlibc.h"
#include "../toriauxlib/c/toriauxlibc_submit.h"
#include "../toriauxlib/core/toriauxlibcore.h"
#include "../toriauxlib/td/toriauxlibtd.h"
#include "osrs/datatypes/appearances.h"
#include "toridraw/toridraw_light_model.h"
#include "toridraw/toridraw_model.h"

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

struct ToriDraw_ModelHandle
runescape_player_body_build(
    struct GameRunescape* game,
    const int appearance[RUNESCAPE_APPEARANCE_SLOT_COUNT])
{
    struct ToriAuxLibC* c;
    struct ToriAuxLibCore* core;
    struct ToriDraw_Model* pieces[RUNESCAPE_APPEARANCE_SLOT_COUNT];
    int piece_count = 0;
    struct ToriDraw_Model* body;
    struct ToriDraw_ModelHandle hnd;
    uint16_t slots[RUNESCAPE_APPEARANCE_SLOT_COUNT];
    int slot;

    if( !game || !game->td || !appearance )
        return (struct ToriDraw_ModelHandle){ .kind = TORIDRAWMK_NONE };

    c = ToriAuxLibTD_C(game->td);
    core = ToriAuxLibTD_Core(game->td);
    if( !c || !core )
        return (struct ToriDraw_ModelHandle){ .kind = TORIDRAWMK_NONE };

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
            if( !ToriAuxLibCore_IdkModelHas(core, (int)op.id) )
                ToriAuxLibC_SubmitIdkModelFromDat1(c, (int)op.id);
            core_piece = ToriAuxLibCore_IdkModelGet(core, (int)op.id);
        }
        else if( op.kind == APPEARANCE_KIND_OBJ )
        {
            if( !ToriAuxLibCore_ObjModelHas(core, (int)op.id) )
                ToriAuxLibC_SubmitObjModelFromDat1(c, (int)op.id);
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
