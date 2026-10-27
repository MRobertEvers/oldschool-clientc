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

/* Reference PlayerComposition recol1d (class294.field3891 in the 239 client):
 * per design part (hair, torso, legs, feet, skin), index 0 is the identity
 * colour the models ship with; colour recolours palette[0] -> palette[colour].
 * Older lanes use a prefix of these: see k_design_colour_counts. */
static int const k_recol_hair[] = { 6798,  107,   10283, 16,    4797,  7744,  5799,  4634,
                                    33697, 22433, 2983,  54193, 8,     5281,  10438, 3650,
                                    38214, 43691, 200,   571,   908,   21830, 28946, 49835,
                                    51526, 43414, 937,   8130,  52114, 30385 };
static int const k_recol_torso[] = { 8741,  12,    64030, 43162, 7735,  8404,  1701,  38430,
                                     24094, 10153, 56621, 4783,  1341,  16578, 35003, 25239,
                                     8,     5281,  10438, 3650,  38214, 43691, 200,   571,
                                     908,   21830, 28946, 49835, 51526 };
static int const k_recol_legs[] = { 25238, 8742,  12,    64030, 43162, 7735,  8404,  1701,
                                    38430, 24094, 10153, 56621, 4783,  1341,  16578, 35003,
                                    8,     5281,  10438, 3650,  38214, 43691, 200,   571,
                                    908,   21830, 28946, 49835, 51526 };
static int const k_recol_feet[] = { 4626, 11146, 6439, 12, 4758, 10270 };
static int const k_recol_skin[] = { 4550,  4537, 5681, 5673,  5790,  6806, 8076,
                                    4574,  17050, 0,   127,  33715, 47545, 918 };

static int const* const k_recol1d[5] = {
    k_recol_hair, k_recol_torso, k_recol_legs, k_recol_feet, k_recol_skin,
};

/* How much of each palette a lane offers, per part. The palettes only ever
 * grew, each era's a prefix of the next (checked against the clients):
 *   LostCity (317-era ClientPlayer.recol1d)   12 16 16 6  8
 *   OSRS 184 (Kronos class211.field2493)      25 29 29 6 13
 *   OSRS 239 and 241 deobs                    30 29 29 6 14
 * There is no client here for OSRS 230..238; they get the 184 counts, which
 * never offer a colour that client lacks. The hair and torso recol2d bands
 * have the same length as recol1d in every one of those clients. */
static int const k_design_colour_counts_lostcity[5] = { 12, 16, 16, 6, 8 };
static int const k_design_colour_counts_osrs184[5] = { 25, 29, 29, 6, 13 };
static int const k_design_colour_counts_osrs239[5] = { 30, 29, 29, 6, 14 };

static int const*
design_colour_counts(struct CacheProvider* provider)
{
    struct RSCache const* profile = CacheProvider_Profile(provider);

    if( !RSCache_IsOsrs(profile) )
        return k_design_colour_counts_lostcity;
    if( profile->revision >= 239 )
        return k_design_colour_counts_osrs239;
    return k_design_colour_counts_osrs184;
}

/* Reference PlayerComposition's secondary colour band (class294.field3890 /
 * field3893 in the 239 client): hair models author their second shade as
 * magenta (55232) and torsos theirs as 9104, each recoloured to
 * palette[colour]. Unlike recol1d, index 0 is NOT the identity for hair —
 * magenta becomes 6554 — so the band is applied for every colour, 0 included. */
static int const k_recol2d_hair[] = { 6554,  115,   10304, 28,    5702,  7756,  5681,  4510,
                                      33701, 22437, 2859,  54197, 16,    5157,  10446, 3658,
                                      38222, 43571, 472,   580,   784,   21966, 28950, 49839,
                                      51534, 43420, 945,   8144,  52122, 30389 };
static int const k_recol2d_torso[] = { 9104,  10275, 7595,  3610,  7975,  8526,  918,   38802,
                                       24466, 10145, 58654, 5027,  1457,  16565, 34991, 25486,
                                       24,    5392,  10429, 3673,  38201, 43579, 192,   687,
                                       412,   21821, 28835, 50076, 51517 };

static struct
{
    int source;
    int const* palette;
} const k_recol2d[2] = {
    { 55232, k_recol2d_hair },
    { 9104, k_recol2d_torso },
};

int
PlayerModel_DesignColourCount(
    struct CacheProvider* provider,
    int part)
{
    assert(provider);
    if( part < 0 || part >= 5 )
        return 0;
    return design_colour_counts(provider)[part];
}

/* PlayerComposition's seven design-part -> wear-slot table (Statics.method8884
 * / class389.field4882 in the 239 client). @see PlayerModel_DesignPartWearpos. */
static int const k_design_part_wearpos[7] = {
    8, 11, 4, 6, 9, 7, 10,
};

int
PlayerModel_DesignPartWearpos(int part)
{
    if( part < 0 || part >= (int)(sizeof(k_design_part_wearpos) / sizeof(int)) )
        return -1;
    return k_design_part_wearpos[part];
}

/* Reference PlayerComposition model build: per design part, recol1d then
 * recol2d, each when the colour indexes the lane's palette. */
static void
player_design_recolour(
    struct CacheProvider* provider,
    struct ToriDraw_Model* model,
    int const colors[5])
{
    int const* counts = design_colour_counts(provider);

    for( int part = 0; part < 5; part++ )
    {
        int colour = colors[part];
        if( colour < 0 || colour >= counts[part] )
            continue;
        ToriDraw_ModelRecolor(model, k_recol1d[part][0], k_recol1d[part][colour]);
        if( part < 2 )
            ToriDraw_ModelRecolor(model, k_recol2d[part].source, k_recol2d[part].palette[colour]);
    }
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
PlayerModel_AppearanceResident(
    struct CacheProvider* provider,
    int const slots[12],
    int gender)
{
    assert(provider);
    assert(slots);

    for( int s = 0; s < 12; s++ )
    {
        int value = slots[s];
        if( Appearance_SlotKind(value) == APPEARANCE_SLOT_KIT )
        {
            struct ToriRS_Idk* idk = CacheProvider_IdkGet(provider, Appearance_SlotKit(value));
            if( !idk )
                return 0;
            for( int m = 0; m < idk->model_ids_count; m++ )
                if( idk->model_ids[m] >= 0 && !CacheProvider_ModelHas(provider, idk->model_ids[m]) )
                    return 0;
        }
        else if( Appearance_SlotKind(value) == APPEARANCE_SLOT_OBJ )
        {
            struct ToriRS_Objtype* obj =
                CacheProvider_ObjtypeGet(provider, Appearance_SlotObj(value));
            int wear[3];
            if( !obj )
                return 0;
            obj_wear_models(obj, gender, wear);
            for( int m = 0; m < 3; m++ )
                if( wear[m] >= 0 && !CacheProvider_ModelHas(provider, wear[m]) )
                    return 0;
        }
    }
    return 1;
}

/*
 * The head twin of PlayerModel_AppearanceResident: is every idk / obj config
 * the appearance names resident, and every HEAD model those configs name for
 * `gender`? Reference ClientPlayer.getHeadModel asks exactly this first
 * (IdkType.checkHead / ObjType.checkHeadModel) and returns null when one part
 * is still coming, because a head merged from the parts that happened to have
 * landed is cached by the caller and never rebuilt.
 */
static int
player_head_appearance_resident(
    struct CacheProvider* provider,
    int const slots[12],
    int gender)
{
    assert(provider);
    assert(slots);

    for( int s = 0; s < 12; s++ )
    {
        int value = slots[s];
        if( Appearance_SlotKind(value) == APPEARANCE_SLOT_KIT )
        {
            struct ToriRS_Idk* idk = CacheProvider_IdkGet(provider, Appearance_SlotKit(value));
            if( !idk )
                return 0;
            for( int h = 0; h < 10; h++ )
                if( idk->heads[h] > 0 && !CacheProvider_ModelHas(provider, idk->heads[h]) )
                    return 0;
        }
        else if( Appearance_SlotKind(value) == APPEARANCE_SLOT_OBJ )
        {
            struct ToriRS_Objtype* obj =
                CacheProvider_ObjtypeGet(provider, Appearance_SlotObj(value));
            int head[2];
            if( !obj )
                return 0;
            obj_head_models(obj, gender, head);
            for( int m = 0; m < 2; m++ )
                if( head[m] >= 0 && !CacheProvider_ModelHas(provider, head[m]) )
                    return 0;
        }
    }
    return 1;
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

    /* All or nothing (reference PlayerComposition.getModel's isReady gate):
     * a body missing a part is not a body, and whoever holds the previous
     * one keeps drawing it until this one can be whole. */
    if( !PlayerModel_AppearanceResident(provider, slots, gender) )
        return NULL;

    for( int s = 0; s < 12; s++ )
    {
        int value = slots[s];
        if( Appearance_SlotKind(value) == APPEARANCE_SLOT_KIT )
        {
            struct ToriRS_Idk* idk =
                CacheProvider_IdkGet(provider, Appearance_SlotKit(value));
            assert(idk);
            for( int m = 0; m < idk->model_ids_count; m++ )
            {
                struct ToriRS_Model* rs;
                struct ToriDraw_Model* model;
                int mid = idk->model_ids[m];
                if( mid < 0 )
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
            assert(obj);
            obj_wear_models(obj, gender, wear);
            for( int m = 0; m < 3; m++ )
            {
                struct ToriRS_Model* rs;
                struct ToriDraw_Model* model;
                if( wear[m] < 0 )
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
        player_design_recolour(provider, merged, colors);

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

    if( !player_head_appearance_resident(provider, slots, gender) )
        return NULL;

    for( int s = 0; s < 12; s++ )
    {
        int value = slots[s];
        if( Appearance_SlotKind(value) == APPEARANCE_SLOT_KIT )
        {
            struct ToriRS_Idk* idk =
                CacheProvider_IdkGet(provider, Appearance_SlotKit(value));
            assert(idk); /* the residency pass above already said yes */
            for( int h = 0; h < 10; h++ )
            {
                struct ToriRS_Model* rs;
                struct ToriDraw_Model* model;
                int mid = idk->heads[h];
                if( mid <= 0 )
                    continue; /* this kit covers no head */
                rs = CacheProvider_ModelGet(provider, mid);
                assert(rs);
                model = ToriDraw_ModelFromToriRS(rs);
                assert(model);
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
            assert(obj); /* the residency pass above already said yes */
            obj_head_models(obj, gender, head);
            for( int m = 0; m < 2; m++ )
            {
                struct ToriRS_Model* rs;
                struct ToriDraw_Model* model;
                if( head[m] < 0 )
                    continue; /* this item covers no head */
                rs = CacheProvider_ModelGet(provider, head[m]);
                assert(rs);
                model = ToriDraw_ModelFromToriRS(rs);
                assert(model);
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
        player_design_recolour(provider, merged, colors);

    /* HD-only textures off before lighting — ModelData.light()'s isSd gate. */
    ToriDraw_ModelDropNonSdTextures(provider, merged);
    /* Left unlit here; the bridge bakes light. Client-TS IfType.getTempModel
     * uses the scene regime; xrsps uses absolute ambient 128 + actor dir when
     * player_head_light_ambient is non-zero. */
    ToriDraw_ModelSetBoundsCylinder(merged);
    ToriDraw_ModelCaptureOriginalVertices(merged);
    return merged;
}
