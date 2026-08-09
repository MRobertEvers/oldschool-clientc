#include "features/features.h"

#include "rscache_profile.h" /* enum RSCache_Epoch */

#include <stddef.h>
#include <string.h>

/*
 * The era tables. Read features.h first: a zero slot means "classic", so the
 * LostCity table is deliberately almost empty and each later era states only
 * what it changed. Keep it that way — a new field must default to the
 * 2004 behaviour so adding one cannot move an existing era.
 */

/* 2004-era / LostCity: everything classic, every slot at its zero default.
 * Written out with explicit designated initializers anyway, because "this era
 * is the zero table" is the fact worth stating. */
static struct ToriRS_FeatureTable const k_features_lostcity = {
    .era = TORIRS_FEATURE_ERA_LOSTCITY,
    .name = "lostcity",
    .pathing_mode = TORIRS_PATHING_CLIENT_BFS,
    .approach_model = TORIRS_APPROACH_LEGACY_SHAPE,
    .npc_approach_uses_size = 0,
    .op_click_nearest_range = 0,
    .nearest_ranks_by_rect_distance = 0,
    .ground_click_nearest_model = TORIRS_NEAREST_RING3_STEPS,
    .los_symmetric_pvp = 0,
    .route_window_tiles = 0,
    .painter_draw_distance = 0,
    .npc_light_uses_type_ambient_contrast = 0,
    .player_head_light_ambient = 0,
    .effects_monophonic = 1,
};

/*
 * OldSchool rev 230-ish. Since end-2013 the server owns routefinding (Ash);
 * MOVE_GAMECLICK is a fixed 5-byte destination body and the client never
 * emits waypoints. Reach uses rsmod's shape-keyed exitStrategy under RECT.
 *
 *   - pathing_mode SERVER_AUTHORITATIVE: no client BFS; server SET_MAP_FLAG.
 *   - approach_model RECT: collision_approach_from_shape on placed shape.
 *   - npc_approach_uses_size: a size-3 NPC is a 3x3 exclusive-rect target.
 *   - op_click_nearest_range 10: rsmod's alternative-route search (server).
 *   - ground_click_nearest_model BOX10_RECT: the same search for a bare ground
 *     or minimap click. The client sends the clicked tile and nothing else, so
 *     "walk as close as you can" is entirely the server's answer; the rev-239
 *     client's own copy of the routine (Statics.method5592) is what the
 *     constants were read from.
 *   - los_symmetric_pvp: the 2019 LMS update — PvP LoS is symmetric; PvM is
 *     not.
 *   - route_window_tiles 128: rsmod's PathFinder floods a fixed 128x128 box
 *     around the mover rather than whatever map is resident.
 */
static struct ToriRS_FeatureTable const k_features_osrs = {
    .era = TORIRS_FEATURE_ERA_OSRS,
    .name = "osrs",
    .pathing_mode = TORIRS_PATHING_SERVER_AUTHORITATIVE,
    .approach_model = TORIRS_APPROACH_RECT,
    .npc_approach_uses_size = 1,
    .op_click_nearest_range = 10,
    .nearest_ranks_by_rect_distance = 1,
    .ground_click_nearest_model = TORIRS_NEAREST_BOX10_RECT,
    .los_symmetric_pvp = 1,
    .route_window_tiles = 128,
    /* Project default for OSRS; the official preference's valid range is
     * 25..90 and a manifest may select any value in it. */
    .painter_draw_distance = 32,
    .npc_light_uses_type_ambient_contrast = 0,
    .player_head_light_ambient = 0,
    /* The modern client mixes effects; only the 2004 one is monophonic. */
    .effects_monophonic = 0,
};

/*
 * Named alias for manifests that already state server_routed. Same pathing
 * mode and approach model as osrs; keeps the xrsps lighting flags.
 *
 * The approach fields still matter: nothing routes on the client, but the
 * same predicates answer "is the player already adjacent?" for the UI.
 */
static struct ToriRS_FeatureTable const k_features_server_routed = {
    .era = TORIRS_FEATURE_ERA_SERVER_ROUTED,
    .name = "server_routed",
    .pathing_mode = TORIRS_PATHING_SERVER_AUTHORITATIVE,
    .approach_model = TORIRS_APPROACH_RECT,
    .npc_approach_uses_size = 1,
    .op_click_nearest_range = 0,
    .nearest_ranks_by_rect_distance = 1,
    /* xrsps runs the same 21x21 alternative-route search for every request. */
    .ground_click_nearest_model = TORIRS_NEAREST_BOX10_RECT,
    .los_symmetric_pvp = 1,
    .route_window_tiles = 128,
    .painter_draw_distance = 0,
    /* xrsps: NpcModelLoader applies type ambient/contrast; player chatheads
     * light with absolute ambient 128 + actor dir (PlayerChatheadFactory). */
    .npc_light_uses_type_ambient_contrast = 1,
    .player_head_light_ambient = 128,
    .effects_monophonic = 0,
};

struct ToriRS_FeatureTable const*
ToriRS_Features_LostCity(void)
{
    return &k_features_lostcity;
}

struct ToriRS_FeatureTable const*
ToriRS_Features_OSRS(void)
{
    return &k_features_osrs;
}

struct ToriRS_FeatureTable const*
ToriRS_Features_ServerRouted(void)
{
    return &k_features_server_routed;
}

struct ToriRS_FeatureTable const*
ToriRS_Features_ByName(char const* name)
{
    if( !name || !name[0] )
        return NULL;
    if( strcmp(name, "lostcity") == 0 )
        return ToriRS_Features_LostCity();
    if( strcmp(name, "osrs") == 0 )
        return ToriRS_Features_OSRS();
    if( strcmp(name, "server_routed") == 0 )
        return ToriRS_Features_ServerRouted();
    return NULL;
}

/* Kept next to the enum's only other definition so a new model cannot be added
 * without a name to state it by. */
static struct
{
    char const* name;
    int model;
} const k_nearest_models[] = {
    { "ring3", TORIRS_NEAREST_RING3_STEPS },
    { "box10_rect", TORIRS_NEAREST_BOX10_RECT },
    { "none", TORIRS_NEAREST_NONE },
};

int
ToriRS_Features_NearestModelByName(char const* name)
{
    if( !name || !name[0] )
        return -1;
    for( size_t i = 0; i < sizeof(k_nearest_models) / sizeof(k_nearest_models[0]); i++ )
    {
        if( strcmp(name, k_nearest_models[i].name) == 0 )
            return k_nearest_models[i].model;
    }
    return -1;
}

int
ToriRS_Features_PainterDrawDistance(struct ToriRS_FeatureTable const* features)
{
    int distance = features ? features->painter_draw_distance : 0;
    if( distance == 0 )
        return TORIRS_PAINTER_DRAW_DISTANCE_MIN;
    if( distance < TORIRS_PAINTER_DRAW_DISTANCE_MIN )
        return TORIRS_PAINTER_DRAW_DISTANCE_MIN;
    if( distance > TORIRS_PAINTER_DRAW_DISTANCE_MAX )
        return TORIRS_PAINTER_DRAW_DISTANCE_MAX;
    return distance;
}

char const*
ToriRS_Features_NearestModelName(int model)
{
    for( size_t i = 0; i < sizeof(k_nearest_models) / sizeof(k_nearest_models[0]); i++ )
    {
        if( k_nearest_models[i].model == model )
            return k_nearest_models[i].name;
    }
    return "?";
}

struct ToriRS_FeatureTable const*
ToriRS_Features_ForCache(int cache_game, int cache_epoch, int cache_revision)
{
    (void)cache_revision; /* lineage decides; see the header. */
    if( cache_epoch == RSCACHE_EPOCH_DAT2 && cache_game == RSCACHE_GAME_OLDSCHOOL )
        return ToriRS_Features_OSRS();
    return ToriRS_Features_LostCity();
}
