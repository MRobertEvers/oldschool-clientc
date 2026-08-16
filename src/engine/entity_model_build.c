#include "entity_model_build.h"

#include "engine/cache_provider.h"
#include "engine/toridraw_model_from_torirs.h"
#include "engine/torirs_types.h"
/* The appearance slot vocabulary (what a slot int means) is defined once, next
 * to every wire spelling of it; the half this file uses is header-only and
 * depends on nothing. See pkt_player_appearance.h. */
#include "net/rev/packets/pkt_player_appearance.h"

#include "toridraw.h"

#include <assert.h>
#include <stddef.h>

enum
{
    PLAYER_MODEL_MAX_PARTS = 16,
};

/* Reference ClientPlayer.recol1d: per design part (hair, torso, legs, feet,
 * skin), index 0 is the identity colour the models ship with; a non-zero
 * colour recolors recol1d[part][0] -> recol1d[part][colour]. */
static int const k_recol_hair[] = { 6798, 107, 10283, 16, 4797, 7744, 5799, 4634,
                                    33697, 22433, 2983, 54193 };
static int const k_recol_torso[] = { 8741, 12, 64030, 43162, 7735, 8404, 1701, 38430,
                                     24094, 10153, 56621, 4783, 1341, 16578, 35003, 25239 };
static int const k_recol_legs[] = { 25238, 8742, 12, 64030, 43162, 7735, 8404, 1701,
                                    38430, 24094, 10153, 56621, 4783, 1341, 16578, 35003 };
static int const k_recol_feet[] = { 4626, 11146, 6439, 12, 4758, 10270 };
static int const k_recol_skin[] = { 4550, 4537, 5681, 5673, 5790, 6806, 8076, 4574 };

/* Reference ClientPlayer.recol2d: the torso's secondary colour band. */
static int const k_recol2d[] = { 9104, 10275, 7595, 3610, 7975, 8526, 918, 38802,
                                 24466, 10145, 58654, 5027, 1457, 16565, 34991, 25486 };

static struct
{
    int const* palette;
    int count;
} const k_recol1d[5] = {
    { k_recol_hair, (int)(sizeof(k_recol_hair) / sizeof(int)) },
    { k_recol_torso, (int)(sizeof(k_recol_torso) / sizeof(int)) },
    { k_recol_legs, (int)(sizeof(k_recol_legs) / sizeof(int)) },
    { k_recol_feet, (int)(sizeof(k_recol_feet) / sizeof(int)) },
    { k_recol_skin, (int)(sizeof(k_recol_skin) / sizeof(int)) },
};

int
PlayerModel_DesignColourCount(int part)
{
    if( part < 0 || part >= (int)(sizeof(k_recol1d) / sizeof(k_recol1d[0])) )
        return 0;
    return k_recol1d[part].count;
}

static void
obj_wear_models(
    struct ToriRS_Objtype const* obj,
    int gender,
    int out_ids[3])
{
    if( gender == 1 )
    {
        out_ids[0] = obj->womanwear;
        out_ids[1] = obj->womanwear2;
        out_ids[2] = obj->womanwear3;
    }
    else
    {
        out_ids[0] = obj->manwear;
        out_ids[1] = obj->manwear2;
        out_ids[2] = obj->manwear3;
    }
}

/* Worn-equipment head model ids (reference ObjType.getHeadModelNoCheck: manhead
 * primary + manhead2 secondary, gendered). out_ids[0] == -1 means the item
 * covers no head (e.g. a body/legs slot, or a hat that hides no hair). */
static void
obj_head_models(
    struct ToriRS_Objtype const* obj,
    int gender,
    int out_ids[2])
{
    if( gender == 1 )
    {
        out_ids[0] = obj->womanhead;
        out_ids[1] = obj->womanhead2;
    }
    else
    {
        out_ids[0] = obj->manhead;
        out_ids[1] = obj->manhead2;
    }
}

int
PlayerModel_CollectAppearanceModelIds(
    struct CacheProvider* provider,
    int const slots[12],
    int gender,
    int* out_ids,
    int cap)
{
    int count = 0;

    assert(provider && slots && out_ids);

    for( int s = 0; s < 12 && count < cap; s++ )
    {
        int value = slots[s];
        if( Appearance_SlotKind(value) == APPEARANCE_SLOT_KIT )
        {
            struct ToriRS_Idk* idk =
                CacheProvider_IdkGet(provider, Appearance_SlotKit(value));
            if( !idk )
                continue;
            for( int m = 0; m < idk->model_ids_count && count < cap; m++ )
                if( idk->model_ids[m] >= 0 )
                    out_ids[count++] = idk->model_ids[m];
        }
        else if( Appearance_SlotKind(value) == APPEARANCE_SLOT_OBJ )
        {
            struct ToriRS_Objtype* obj =
                CacheProvider_ObjtypeGet(provider, Appearance_SlotObj(value));
            int wear[3];
            if( !obj )
                continue;
            obj_wear_models(obj, gender, wear);
            for( int m = 0; m < 3 && count < cap; m++ )
                if( wear[m] >= 0 )
                    out_ids[count++] = wear[m];
        }
    }
    return count;
}

int
PlayerHeadModel_CollectHeadModelIds(
    struct CacheProvider* provider,
    int const slots[12],
    int gender,
    int* out_ids,
    int cap)
{
    int count = 0;

    assert(provider && slots && out_ids);

    for( int s = 0; s < 12 && count < cap; s++ )
    {
        int value = slots[s];
        if( Appearance_SlotKind(value) == APPEARANCE_SLOT_KIT )
        {
            struct ToriRS_Idk* idk =
                CacheProvider_IdkGet(provider, Appearance_SlotKit(value));
            if( !idk )
                continue;
            for( int h = 0; h < 10 && count < cap; h++ )
                if( idk->heads[h] > 0 )
                    out_ids[count++] = idk->heads[h];
        }
        else if( Appearance_SlotKind(value) == APPEARANCE_SLOT_OBJ )
        {
            struct ToriRS_Objtype* obj =
                CacheProvider_ObjtypeGet(provider, Appearance_SlotObj(value));
            int head[2];
            if( !obj )
                continue;
            obj_head_models(obj, gender, head);
            for( int m = 0; m < 2 && count < cap; m++ )
                if( head[m] >= 0 )
                    out_ids[count++] = head[m];
        }
    }
    return count;
}

static int
append_model(
    struct ToriDraw_Model** parts,
    int part_count,
    struct ToriDraw_Model* model)
{
    if( !model )
        return part_count;
    if( part_count < PLAYER_MODEL_MAX_PARTS )
        parts[part_count++] = model;
    else
        ToriDraw_ModelFree(model);
    return part_count;
}

struct ToriDraw_Model*
PlayerModel_BuildFromAppearance(
    struct CacheProvider* provider,
    int const slots[12],
    int const colors[5],
    int gender)
{
    struct ToriDraw_Model* parts[PLAYER_MODEL_MAX_PARTS];
    struct ToriDraw_Model* merged;
    int part_count = 0;

    assert(provider && slots);

    for( int s = 0; s < 12; s++ )
    {
        int value = slots[s];
        if( Appearance_SlotKind(value) == APPEARANCE_SLOT_KIT )
        {
            struct ToriRS_Idk* idk =
                CacheProvider_IdkGet(provider, Appearance_SlotKit(value));
            if( !idk )
                continue;
            for( int m = 0; m < idk->model_ids_count; m++ )
            {
                struct ToriRS_Model* rs;
                struct ToriDraw_Model* model;
                int mid = idk->model_ids[m];
                if( mid < 0 || !CacheProvider_ModelHas(provider, mid) )
                    continue;
                rs = CacheProvider_ModelGet(provider, mid);
                model = rs ? ToriDraw_ModelFromToriRS(rs) : NULL;
                if( !model )
                    continue;
                /* Kit recolors per part before merge (reference IdkType
                 * getModelNoCheck). */
                for( int r = 0; r < 10; r++ )
                    if( idk->recolors_from[r] != 0 || idk->recolors_to[r] != 0 )
                        ToriDraw_ModelRecolor(model, idk->recolors_from[r], idk->recolors_to[r]);
                part_count = append_model(parts, part_count, model);
            }
        }
        else if( Appearance_SlotKind(value) == APPEARANCE_SLOT_OBJ )
        {
            struct ToriRS_Objtype* obj =
                CacheProvider_ObjtypeGet(provider, Appearance_SlotObj(value));
            int wear[3];
            if( !obj )
                continue;
            obj_wear_models(obj, gender, wear);
            for( int m = 0; m < 3; m++ )
            {
                struct ToriRS_Model* rs;
                struct ToriDraw_Model* model;
                if( wear[m] < 0 || !CacheProvider_ModelHas(provider, wear[m]) )
                    continue;
                rs = CacheProvider_ModelGet(provider, wear[m]);
                model = rs ? ToriDraw_ModelFromToriRS(rs) : NULL;
                if( !model )
                    continue;
                /* Obj recolors (reference ObjType getWearModelNoCheck). */
                for( int r = 0; r < obj->recolor_count; r++ )
                    ToriDraw_ModelRecolor(model, obj->recolors_from[r], obj->recolors_to[r]);
                part_count = append_model(parts, part_count, model);
            }
        }
    }

    if( part_count == 0 )
        return NULL;

    merged = ToriDraw_ModelNewMerge(parts, part_count);
    for( int p = 0; p < part_count; p++ )
        ToriDraw_ModelFree(parts[p]);
    if( !merged )
        return NULL;

    /* Design recolors after the merge (reference ClientPlayer 517-528). */
    if( colors )
    {
        for( int part = 0; part < 5; part++ )
        {
            int colour = colors[part];
            if( colour <= 0 || colour >= k_recol1d[part].count )
                continue;
            ToriDraw_ModelRecolor(merged, k_recol1d[part].palette[0], k_recol1d[part].palette[colour]);
            if( part == 1 && colour < (int)(sizeof(k_recol2d) / sizeof(int)) )
                ToriDraw_ModelRecolor(merged, k_recol2d[0], k_recol2d[colour]);
        }
    }

    /* HD-only textures off before lighting — ModelData.light()'s isSd gate. */
    ToriDraw_ModelDropNonSdTextures(provider, merged);
    {
        struct ToriDraw_ModelHandle hnd;
        hnd.kind = TORIDRAWMK_MODEL;
        hnd.u.model.model = merged;
        hnd.u.model.ground = NULL;
        ToriDraw_LightModelActor(hnd, 0, 0);
    }
    ToriDraw_ModelSetBoundsCylinder(merged);
    ToriDraw_ModelCaptureOriginalVertices(merged);
    return merged;
}

struct ToriDraw_Model*
PlayerHeadModel_BuildFromAppearance(
    struct CacheProvider* provider,
    int const slots[12],
    int const colors[5],
    int gender)
{
    struct ToriDraw_Model* parts[PLAYER_MODEL_MAX_PARTS];
    struct ToriDraw_Model* merged;
    int part_count = 0;

    assert(provider && slots);

    for( int s = 0; s < 12; s++ )
    {
        int value = slots[s];
        if( Appearance_SlotKind(value) == APPEARANCE_SLOT_KIT )
        {
            struct ToriRS_Idk* idk =
                CacheProvider_IdkGet(provider, Appearance_SlotKit(value));
            if( !idk )
                continue;
            for( int h = 0; h < 10; h++ )
            {
                struct ToriRS_Model* rs;
                struct ToriDraw_Model* model;
                int mid = idk->heads[h];
                if( mid <= 0 || !CacheProvider_ModelHas(provider, mid) )
                    continue;
                rs = CacheProvider_ModelGet(provider, mid);
                model = rs ? ToriDraw_ModelFromToriRS(rs) : NULL;
                if( !model )
                    continue;
                for( int r = 0; r < 10; r++ )
                    if( idk->recolors_from[r] != 0 || idk->recolors_to[r] != 0 )
                        ToriDraw_ModelRecolor(
                            model, idk->recolors_from[r], idk->recolors_to[r]);
                part_count = append_model(parts, part_count, model);
            }
        }
        else if( Appearance_SlotKind(value) == APPEARANCE_SLOT_OBJ )
        {
            /* Worn-equipment head models (reference getHeadModel pulls
             * ObjType.getHeadModelNoCheck for slots >= 512): the gendered
             * manhead + manhead2 with the obj's own recolours. head[0] == -1
             * means the item covers no head. */
            struct ToriRS_Objtype* obj =
                CacheProvider_ObjtypeGet(provider, Appearance_SlotObj(value));
            int head[2];
            if( !obj )
                continue;
            obj_head_models(obj, gender, head);
            for( int m = 0; m < 2; m++ )
            {
                struct ToriRS_Model* rs;
                struct ToriDraw_Model* model;
                if( head[m] < 0 || !CacheProvider_ModelHas(provider, head[m]) )
                    continue;
                rs = CacheProvider_ModelGet(provider, head[m]);
                model = rs ? ToriDraw_ModelFromToriRS(rs) : NULL;
                if( !model )
                    continue;
                for( int r = 0; r < obj->recolor_count; r++ )
                    ToriDraw_ModelRecolor(model, obj->recolors_from[r], obj->recolors_to[r]);
                part_count = append_model(parts, part_count, model);
            }
        }
    }

    if( part_count == 0 )
        return NULL;

    merged = ToriDraw_ModelNewMerge(parts, part_count);
    for( int p = 0; p < part_count; p++ )
        ToriDraw_ModelFree(parts[p]);
    if( !merged )
        return NULL;

    /* Design recolours after the merge — same palettes as the body (reference
     * getHeadModel reuses recol1d/recol2d). Only hair/skin actually appear on a
     * bare head, but applying the full set is harmless (identity for absent
     * colours). */
    if( colors )
    {
        for( int part = 0; part < 5; part++ )
        {
            int colour = colors[part];
            if( colour <= 0 || colour >= k_recol1d[part].count )
                continue;
            ToriDraw_ModelRecolor(merged, k_recol1d[part].palette[0], k_recol1d[part].palette[colour]);
            if( part == 1 && colour < (int)(sizeof(k_recol2d) / sizeof(int)) )
                ToriDraw_ModelRecolor(merged, k_recol2d[0], k_recol2d[colour]);
        }
    }

    /* HD-only textures off before lighting — ModelData.light()'s isSd gate. */
    ToriDraw_ModelDropNonSdTextures(provider, merged);
    /* Left unlit here; the bridge bakes light. Client-TS IfType.getTempModel
     * uses the scene regime; xrsps uses absolute ambient 128 + actor dir when
     * player_head_light_ambient is non-zero. */
    ToriDraw_ModelSetBoundsCylinder(merged);
    ToriDraw_ModelCaptureOriginalVertices(merged);
    return merged;
}
